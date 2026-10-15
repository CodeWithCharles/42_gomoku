/* -------------------------------------------------------------------------- */
/*                                   Session                                  */
/* -------------------------------------------------------------------------- */

#include "server/session.hpp"

#include <algorithm>
#include <utility>
#include <nlohmann/json.hpp>
#include <vector>

namespace gomoku::server {
namespace {

inline constexpr std::chrono::milliseconds kAiPace{300};
inline constexpr int kMaxDepthLimit = 64;
inline constexpr int kMinBudgetMs = 10;
inline constexpr int kMaxBudgetMs = 60000;
inline constexpr int kMaxWeight = 200000;

using nlohmann::json;

// Protocol name of a colour.
const char* colourName(Player p) {
	switch (p) {
		case Player::Black:
			return "black";
		case Player::White:
			return "white";
		default:
			return "none";
	}
}

// Protocol name of a game status.
const char* statusName(GameStatus status) {
	switch (status) {
		case GameStatus::Ongoing:
			return "ongoing";
		case GameStatus::BlackWins:
			return "black_wins";
		case GameStatus::WhiteWins:
			return "white_wins";
		case GameStatus::Draw:
			return "draw";
	}
	return "ongoing";
}

// Protocol name of an opening convention.
const char* openingName(Opening opening) {
	switch (opening) {
		case Opening::Standard:
			return "standard";
		case Opening::Pro:
			return "pro";
		case Opening::Swap:
			return "swap";
		case Opening::Swap2:
			return "swap2";
		case Opening::LongPro:
			return "longpro";
	}
	return "standard";
}

// Opening convention behind a protocol name.
Opening openingFrom(const std::string& name) {
	if (name == "pro") {
		return Opening::Pro;
	}
	if (name == "swap") {
		return Opening::Swap;
	}
	if (name == "swap2") {
		return Opening::Swap2;
	}
	if (name == "longpro") {
		return Opening::LongPro;
	}
	return Opening::Standard;
}

// Protocol name of an opening step.
const char* openingStepName(OpeningStep step) {
	switch (step) {
		case OpeningStep::Place:
			return "place";
		case OpeningStep::Choose:
			return "choose";
		case OpeningStep::Play:
			break;
	}
	return "play";
}

// Protocol name of a choice of colour.
const char* choiceName(OpeningChoice choice) {
	switch (choice) {
		case OpeningChoice::White:
			return "white";
		case OpeningChoice::PlaceTwo:
			return "place2";
		case OpeningChoice::Black:
			break;
	}
	return "black";
}

// Search statistics as the Stats type of ui/src/protocol.ts.
json statsOf(const SearchStats& stats) {
	json out;
	out["depth"] = stats.depth;
	out["score"] = stats.score;
	out["best"] = static_cast<int>(stats.best);
	out["nodes"] = stats.nodes;
	out["leaves"] = stats.leaves;
	out["cutoffs"] = stats.cutoffs;
	out["ttHits"] = stats.ttHits;
	out["elapsedMs"] = stats.elapsedMs;

	json pv = json::array();
	for (const Idx move : stats.pv) {
		pv.push_back(static_cast<int>(move));
	}
	out["pv"] = std::move(pv);

	json roots = json::array();
	for (const auto& entry : stats.rootScores) {
		roots.push_back({{"idx", static_cast<int>(entry.first)}, {"score", entry.second}});
	}
	out["rootScores"] = std::move(roots);
	return out;
}

// Integer field `key` of `source`, or `fallback` when it is missing, not a
// number, or outside [lo, hi]. json::value would convert 1e30 or 2.5e9 to int
// directly, which is undefined behaviour.
int intField(const json& source, const char* key, int fallback, int lo, int hi) {
	const auto found = source.find(key);
	if (found == source.end() || !found->is_number()) {
		return fallback;
	}
	const double value = found->get<double>();
	if (!(value >= lo && value <= hi)) {
		return fallback;
	}
	return static_cast<int>(value);
}

// Parses a command body, never throwing on malformed input.
json parseCommand(const std::string& text) {
	return json::parse(text, nullptr, false);
}

}  // namespace

// True when `p` is played by the engine.
bool Seats::isAi(Player p) const {
	return p == Player::Black ? blackIsAi : whiteIsAi;
}

Session::Session(net::Server& server) : server_(server) {}

// Stops the worker: interrupts the search, wakes the queue and joins.
Session::~Session() {
	{
		const std::lock_guard<std::mutex> lock(queueMutex_);
		closing_ = true;
	}
	interruptAi();
	queueReady_.notify_all();
	if (worker_.joinable()) {
		worker_.join();
	}
}

// Wires the connection and message handlers onto the server, then starts the
// worker thread that owns the game.
void Session::install() {
	server_.onConnect([this](net::ClientId client) { onConnect(client); });
	server_.onText([this](net::ClientId client, const std::string& text) { onText(client, text); });
	worker_ = std::thread([this] { workerLoop(); });
}

// A client just finished the handshake: the worker will hand it the state.
void Session::onConnect(net::ClientId client) {
	try {
		enqueue(Job{JobKind::Connect, client, {}});
	} catch (...) {
		return;
	}
}

// Routes one frame, on a civetweb thread. Only `stop` and `pause` act here;
// every other command goes to the worker, new-game and undo interrupting the
// AI first. Stop and pause still queue a job so the worker reports the new
// state.
// `suggest` gets its own kind so the queue can coalesce and drop it.
// Nothing may escape: an exception crossing a civetweb callback is fatal.
void Session::onText(net::ClientId client, const std::string& text) {
	try {
		const json command = parseCommand(text);
		const json type = command.is_object() ? command.value("type", json{}) : json{};
		if (type == "stop") {
			interruptAi();
		}
		if (type == "pause") {
			pauseAi();
		}
		if (type == "new-game" || type == "undo") {
			interruptAi();
		}
		const JobKind kind = type == "suggest" ? JobKind::Suggest : JobKind::Command;
		enqueue(Job{kind, client, text});
	} catch (...) {
		return;
	}
}

// Appends a job for the worker, unless the session is closing. A new client
// jumps the queue so it gets its state right after the current job, and a
// suggest already waiting makes another one pointless: searches are the only
// jobs that cost time, so this keeps the queue short under any flood.
void Session::enqueue(Job job) {
	{
		const std::lock_guard<std::mutex> lock(queueMutex_);
		if (closing_) {
			return;
		}
		const auto isSuggest = [](const Job& queued) { return queued.kind == JobKind::Suggest; };
		if (job.kind == JobKind::Connect) {
			jobs_.push_front(std::move(job));
		} else if (job.kind != JobKind::Suggest ||
				   std::none_of(jobs_.begin(), jobs_.end(), isSuggest)) {
			jobs_.push_back(std::move(job));
		}
	}
	queueReady_.notify_one();
}

// Worker thread: runs the queued jobs one by one until the session closes.
void Session::workerLoop() {
	std::unique_lock<std::mutex> lock(queueMutex_);
	for (;;) {
		queueReady_.wait(lock, [this] { return closing_ || !jobs_.empty(); });
		if (closing_) {
			return;
		}
		const Job job = std::move(jobs_.front());
		jobs_.pop_front();
		lock.unlock();
		run(job);
		lock.lock();
	}
}

// Runs one job on the worker. Nothing may escape: an exception leaving a
// thread calls std::terminate.
void Session::run(const Job& job) {
	try {
		switch (job.kind) {
			case JobKind::Connect:
				pushState(job.client, "connected");
				break;
			case JobKind::Command:
			case JobKind::Suggest:
				dispatch(job.client, job.text);
				break;
			case JobKind::AiTurn:
				aiTurn(job.client);
				break;
		}
	} catch (const std::exception& error) {
		pushError(job.client, error.what(), "unknown");
	} catch (...) {
		pushError(job.client, "internal error", "unknown");
	}
}

// Stops the running search, drops the suggests still waiting and pauses the
// AI. Safe from any thread. The flags are set under the queue lock so a worker
// about to wait between two AI moves cannot miss the wake-up.
void Session::interruptAi() {
	{
		const std::lock_guard<std::mutex> lock(queueMutex_);
		halted_.store(true, std::memory_order_relaxed);
		paused_.store(true, std::memory_order_relaxed);
		std::erase_if(jobs_, [](const Job& queued) { return queued.kind == JobKind::Suggest; });
	}
	engine_.requestStop();
	queueReady_.notify_all();
}

// Keeps the AI from starting another action, letting the running search end
// and its move be played. Safe from any thread, same locking as interruptAi.
void Session::pauseAi() {
	{
		const std::lock_guard<std::mutex> lock(queueMutex_);
		paused_.store(true, std::memory_order_relaxed);
	}
	queueReady_.notify_all();
}

// Executes one command on the worker.
void Session::dispatch(net::ClientId client, const std::string& text) {
	const json command = parseCommand(text);
	if (command.is_discarded() || !command.is_object()) {
		pushError(client, "malformed JSON", "unknown");
		return;
	}
	const std::string type = command.value("type", std::string{});
	if (type == "new-game") {
		newGame(client, text);
	} else if (type == "play") {
		play(client, text);
	} else if (type == "suggest") {
		suggest(client);
	} else if (type == "undo") {
		undo(client);
	} else if (type == "choose") {
		chooseColour(client, text);
	} else if (type == "limits") {
		setLimits(client, text);
	} else if (type == "weights") {
		setWeights(client, text);
	} else if (type == "stop") {
		pushState(client, "stop");
	} else if (type == "pause") {
		pushState(client, "pause");
	} else if (type == "resume") {
		resume(client);
	} else if (type == "step") {
		step(client);
	} else {
		pushError(client, "unknown command: " + type, "unknown");
	}
}

// Restarts a game under the requested settings and seats.
void Session::newGame(net::ClientId client, const std::string& body) {
	const json command = parseCommand(body);

	if (seatsSwapped_) {
		std::swap(seats_.blackIsAi, seats_.whiteIsAi);
		seatsSwapped_ = false;
	}

	GameConfig config;
	if (command.contains("config") && command.at("config").is_object()) {
		const json& source = command.at("config");
		config.capturesEnabled = source.value("captures", true);
		config.doubleThreeForbidden = source.value("doubleThree", true);
		config.endgameCapture = source.value("endgameCapture", true);
		config.opening = openingFrom(source.value("opening", std::string{"standard"}));
	}
	if (command.contains("players") && command.at("players").is_object()) {
		const json& source = command.at("players");
		seats_.blackIsAi = source.value("black", std::string{"human"}) == "ai";
		seats_.whiteIsAi = source.value("white", std::string{"ai"}) == "ai";
	}

	game_.reset(config);
	lastStats_ = SearchStats{};
	moveStats_.clear();
	choiceStats_.clear();
	bank_.reset();
	stepPending_ = false;
	halted_.store(false, std::memory_order_relaxed);
	paused_.store(false, std::memory_order_relaxed);
	pushState(client, "new_game");
	scheduleAiTurn(client);
}

// Plays one human move, then lets the AI answer.
void Session::play(net::ClientId client, const std::string& body) {
	const json command = parseCommand(body);

	int index = intField(command, "idx", -1, 0, kCellCount - 1);
	if (index < 0 && command.contains("x") && command.contains("y")) {
		const int x = intField(command, "x", -1, 0, kBoardSize - 1);
		const int y = intField(command, "y", -1, 0, kBoardSize - 1);
		if (inBounds(x, y)) {
			index = idxOf(x, y);
		}
	}
	if (index < 0 || index >= kCellCount) {
		pushError(client, legalityText(Legality::OutOfBounds), legalityCode(Legality::OutOfBounds));
		return;
	}

	const Legality verdict = game_.play(static_cast<Idx>(index));
	if (verdict != Legality::Legal) {
		pushError(client, legalityText(verdict), legalityCode(verdict));
		return;
	}
	alignStats();
	halted_.store(false, std::memory_order_relaxed);
	paused_.store(false, std::memory_order_relaxed);
	pushState(client, "move");
	scheduleAiTurn(client);
}

// Runs a search without playing, and reprots the move it would pick.
void Session::suggest(net::ClientId client) {
	if (game_.status() != GameStatus::Ongoing) {
		pushError(client, legalityText(Legality::GameOver), legalityCode(Legality::GameOver));
		return;
	}
	if (game_.openingStep() == OpeningStep::Choose) {
		pushError(client, legalityText(Legality::OpeningRule), legalityCode(Legality::OpeningRule));
		return;
	}
	const SearchResult result = think(client, game_);
	lastStats_ = result.stats;
	pushState(client, "suggestion");
}

// Takes back moves until a human is to act again. Undoing past a choice of
// colour cancels it, and the seats follow.
void Session::undo(net::ClientId client) {
	game_.undo();
	syncSeats();
	if (seats_.isAi(game_.actor())) {
		game_.undo();
		syncSeats();
	}
	lastStats_ = SearchStats{};
	alignStats();
	pushState(client, "undo");
}

// Applies a human's choice during a Swap or Swap2 opening: "black", "white",
// or "place2" to lay two more stones under Swap2.
void Session::chooseColour(net::ClientId client, const std::string& body) {
	const json command = parseCommand(body);
	const json option = command.value("color", json{});
	OpeningChoice choice = OpeningChoice::Black;
	if (option == "white") {
		choice = OpeningChoice::White;
	} else if (option == "place2") {
		choice = OpeningChoice::PlaceTwo;
	} else if (option != "black") {
		pushError(client, "choose expects black, white or place2", "unknown");
		return;
	}
	if (game_.openingStep() == OpeningStep::Choose && seats_.isAi(game_.actor())) {
		pushError(client, "the engine makes this choice", legalityCode(Legality::NotYourTurn));
		return;
	}
	const Legality verdict = game_.choose(choice);
	if (verdict != Legality::Legal) {
		pushError(client, legalityText(verdict), legalityCode(verdict));
		return;
	}
	syncSeats();
	alignStats();
	halted_.store(false, std::memory_order_relaxed);
	paused_.store(false, std::memory_order_relaxed);
	pushState(client, "choice");
	scheduleAiTurn(client);
}

// Retunes the search. Out of range values are ignored.
void Session::setLimits(net::ClientId client, const std::string& body) {
	const json command = parseCommand(body);
	limits_.maxDepth = intField(command, "maxDepth", limits_.maxDepth, 1, kMaxDepthLimit);
	limits_.budget = std::chrono::milliseconds(intField(
		command, "budgetMs", static_cast<int>(limits_.budget.count()), kMinBudgetMs, kMaxBudgetMs));
	pushState(client, "limits");
}

// Retunes the heuristic. Each weight stays in [0, kMaxWeight], which keeps
// every sum of the evaluation far from int overflow.
void Session::setWeights(net::ClientId client, const std::string& body) {
	const json command = parseCommand(body);
	EvalWeights weights = eval::weights();
	if (command.contains("weights") && command.at("weights").is_object()) {
		const json& source = command.at("weights");
		weights.five = intField(source, "five", weights.five, 0, kMaxWeight);
		weights.openFour = intField(source, "openFour", weights.openFour, 0, kMaxWeight);
		weights.simpleFour = intField(source, "simpleFour", weights.simpleFour, 0, kMaxWeight);
		weights.openThree = intField(source, "openThree", weights.openThree, 0, kMaxWeight);
		weights.brokenThree = intField(source, "brokenThree", weights.brokenThree, 0, kMaxWeight);
		weights.simpleThree = intField(source, "simpleThree", weights.simpleThree, 0, kMaxWeight);
		weights.openTwo = intField(source, "openTwo", weights.openTwo, 0, kMaxWeight);
		weights.capturedPair =
			intField(source, "capturedPair", weights.capturedPair, 0, kMaxWeight);
		weights.captureThreat =
			intField(source, "captureThreat", weights.captureThreat, 0, kMaxWeight);
	}
	eval::setWeights(weights);
	pushState(client, "weights");
}

// Searches, pushing a progress event at every completed depth. The search
// clears the engine's stop flag when it starts, so a stop that came just
// before would be lost: each progress event re-arms it while halted_ is set.
SearchResult Session::think(net::ClientId client, Game& game) {
	const ProgressFn onProgress = [this, client](const SearchStats& stats) {
		if (halted_.load(std::memory_order_relaxed)) {
			engine_.requestStop();
		}
		json event = statsOf(stats);
		event["type"] = "progress";
		server_.sendText(client, event.dump());
	};
	SearchLimits limits = limits_;
	limits.budget = bank_.next(limits_.budget);
	const SearchResult result = engine_.search(game, limits, onProgress);
	bank_.record(result.stats.elapsedMs);
	return result;
}

// Copy of the game under the same rules but a Standard opening, replayed
// move by move, so the engine can search a position waiting for a choice.
Game Session::freePlayCopy() const {
	GameConfig config = game_.config();
	config.opening = Opening::Standard;
	Game copy(config);
	for (const PlayedMove& move : game_.history()) {
		copy.play(move.idx);
	}
	return copy;
}

// Keeps the seats in step with the game: when a choice of colour swaps the
// players, or an undo cancels one, the human and AI flags trade places.
void Session::syncSeats() {
	if (game_.colorsSwapped() != seatsSwapped_) {
		std::swap(seats_.blackIsAi, seats_.whiteIsAi);
		seatsSwapped_ = !seatsSwapped_;
	}
}

// Queues one AI move if the AI is to play and nothing asked it to hold, a
// pending step overriding a pause. At most one is ever pending.
void Session::scheduleAiTurn(net::ClientId client) {
	if (aiPending_ || halted_.load(std::memory_order_relaxed) ||
		(paused_.load(std::memory_order_relaxed) && !stepPending_) ||
		game_.status() != GameStatus::Ongoing || !seats_.isAi(game_.actor())) {
		return;
	}
	aiPending_ = true;
	enqueue(Job{JobKind::AiTurn, client, {}});
}

// Plays one AI move, then queues the next. Between two engines it first waits
// kAiPace so the game can be watched; a stop or a pause ends the wait, any
// queued command cuts it short and goes first. A step skips the wait.
void Session::aiTurn(net::ClientId client) {
	aiPending_ = false;
	const bool stepping = stepPending_;
	stepPending_ = false;
	if (halted_.load(std::memory_order_relaxed) ||
		(paused_.load(std::memory_order_relaxed) && !stepping) ||
		game_.status() != GameStatus::Ongoing || !seats_.isAi(game_.actor())) {
		return;
	}
	if (!stepping && seats_.blackIsAi && seats_.whiteIsAi) {
		std::unique_lock<std::mutex> lock(queueMutex_);
		const bool woken = queueReady_.wait_for(lock, kAiPace, [this] {
			return closing_ || !jobs_.empty() || halted_.load(std::memory_order_relaxed) ||
				   paused_.load(std::memory_order_relaxed);
		});
		lock.unlock();
		if (woken) {
			scheduleAiTurn(client);
			return;
		}
	}
	if (game_.openingStep() == OpeningStep::Choose) {
		aiChoose(client);
		return;
	}
	const SearchResult result = think(client, game_);
	lastStats_ = result.stats;
	if (result.best == kNoIdx) {
		return;
	}
	if (game_.play(result.best) != Legality::Legal) {
		pushError(client, "engine proposed an illegal move", "unknown");
		return;
	}
	alignStats();
	moveStats_.back() = result.stats;
	pushState(client, "ai_move");
	scheduleAiTurn(client);
}

// Lets the AI choose its colour: it searches the position as it stands and
// takes the side the score favours. It never asks for two more stones.
void Session::aiChoose(net::ClientId client) {
	Game probe = freePlayCopy();
	const SearchResult result = think(client, probe);
	lastStats_ = result.stats;
	const Player side = probe.toMove();
	const Player pick = result.stats.score >= 0 ? side : opponent(side);
	if (game_.choose(pick == Player::Black ? OpeningChoice::Black : OpeningChoice::White) !=
		Legality::Legal) {
		return;
	}
	syncSeats();
	alignStats();
	choiceStats_.back() = result.stats;
	pushState(client, "choice");
	scheduleAiTurn(client);
}

// Lifts a pause or a stop and lets the AI carry on.
void Session::resume(net::ClientId client) {
	stepPending_ = false;
	halted_.store(false, std::memory_order_relaxed);
	paused_.store(false, std::memory_order_relaxed);
	pushState(client, "resume");
	scheduleAiTurn(client);
}

// Lets the AI take exactly one action, then stay paused. Does nothing when no
// AI is to act.
void Session::step(net::ClientId client) {
	halted_.store(false, std::memory_order_relaxed);
	paused_.store(true, std::memory_order_relaxed);
	stepPending_ = true;
	scheduleAiTurn(client);
	if (!aiPending_) {
		stepPending_ = false;
	}
}

// Sizes the per-action statistics to the game, so they follow every move,
// choice and undo. New entries carry no statistics.
void Session::alignStats() {
	moveStats_.resize(game_.history().size());
	choiceStats_.resize(game_.choices().size());
}

// Serialises the whole game state, 361 cells included.
std::string Session::buildState(const char* event) const {
	const Board& board = game_.board();

	std::vector<int> cells;
	cells.reserve(kCellCount);
	for (Idx i = 0; i < kCellCount; ++i) {
		cells.push_back(static_cast<int>(board.at(i)));
	}

	json history = json::array();
	const std::vector<PlayedMove>& moves = game_.history();
	for (std::size_t i = 0; i < moves.size(); ++i) {
		const PlayedMove& move = moves[i];
		json captured = json::array();
		for (int k = 0; k < move.capturedCount; ++k) {
			captured.push_back(static_cast<int>(move.captured[k]));
		}
		json entry = {{"idx", static_cast<int>(move.idx)},
					  {"player", colourName(move.player)},
					  {"captured", std::move(captured)}};
		if (i < moveStats_.size() && moveStats_[i]) {
			entry["stats"] = statsOf(*moveStats_[i]);
		}
		history.push_back(std::move(entry));
	}

	json choices = json::array();
	const std::vector<ChoiceRecord>& records = game_.choices();
	for (std::size_t i = 0; i < records.size(); ++i) {
		json entry = {{"afterMove", records[i].moves},
					  {"choice", choiceName(records[i].choice)},
					  {"swapped", records[i].swaps},
					  {"actor", colourName(records[i].actor)}};
		if (i < choiceStats_.size() && choiceStats_[i]) {
			entry["stats"] = statsOf(*choiceStats_[i]);
		}
		choices.push_back(std::move(entry));
	}

	json state;
	state["type"] = "state";
	state["event"] = event;
	state["board"] = std::move(cells);
	state["size"] = kBoardSize;
	state["toMove"] = colourName(game_.toMove());
	state["status"] = statusName(game_.status());
	state["winReason"] = winReasonCode(game_.winReason());
	state["pairs"] = {{"black", game_.pairsTaken(Player::Black)},
					  {"white", game_.pairsTaken(Player::White)}};
	state["players"] = {{"black", seats_.blackIsAi ? "ai" : "human"},
						{"white", seats_.whiteIsAi ? "ai" : "human"}};
	state["config"] = {{"captures", game_.config().capturesEnabled},
					   {"doubleThree", game_.config().doubleThreeForbidden},
					   {"endgameCapture", game_.config().endgameCapture},
					   {"opening", openingName(game_.config().opening)}};
	state["limits"] = {{"maxDepth", limits_.maxDepth},
					   {"budgetMs", static_cast<long long>(limits_.budget.count())},
					   {"maxCandidates", limits_.maxCandidates}};
	state["history"] = std::move(history);
	state["choices"] = std::move(choices);
	state["paused"] = paused_.load(std::memory_order_relaxed);
	json options = json::array();
	if (game_.openingStep() == OpeningStep::Choose) {
		options.push_back("black");
		options.push_back("white");
		if (game_.canPlaceTwo()) {
			options.push_back("place2");
		}
	}
	state["opening"] = {{"step", openingStepName(game_.openingStep())},
						{"actor", colourName(game_.actor())},
						{"options", std::move(options)}};
	state["lastStats"] = statsOf(lastStats_);
	return state.dump();
}

// Pushes the full state to one client.
void Session::pushState(net::ClientId client, const char* event) {
	server_.sendText(client, buildState(event));
}

// Pushes an error, leaving the connection open.
void Session::pushError(net::ClientId client, const std::string& message, const char* code) {
	json error;
	error["type"] = "error";
	error["message"] = message;
	error["code"] = code;
	server_.sendText(client, error.dump());
}

}  // namespace gomoku::server
