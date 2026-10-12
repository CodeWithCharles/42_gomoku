/* -------------------------------------------------------------------------- */
/*                                   Session                                  */
/* -------------------------------------------------------------------------- */

#include "server/session.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <vector>

namespace gomoku::server {
namespace {

inline constexpr int64_t kBudgetCap = 2;
inline constexpr std::chrono::milliseconds kAiPace{300};

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
	return Opening::Standard;
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

// Routes one frame, on a civetweb thread. Only `stop` acts here; every other
// command goes to the worker, new-game and undo interrupting the AI first.
// Nothing may escape: an exception crossing a civetweb callback is fatal.
void Session::onText(net::ClientId client, const std::string& text) {
	try {
		const json command = parseCommand(text);
		const json type = command.is_object() ? command.value("type", json{}) : json{};
		if (type == "stop") {
			interruptAi();
			return;
		}
		if (type == "new-game" || type == "undo") {
			interruptAi();
		}
		enqueue(Job{JobKind::Command, client, text});
	} catch (...) {
		return;
	}
}

// Appends a job for the worker, unless the session is closing.
void Session::enqueue(Job job) {
	{
		const std::lock_guard<std::mutex> lock(queueMutex_);
		if (closing_) {
			return;
		}
		jobs_.push_back(std::move(job));
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

// Stops the running search and keeps the AI from starting another move. Safe
// from any thread. The flag is set under the queue lock so a worker about to
// pause between two AI moves cannot miss the wake-up.
void Session::interruptAi() {
	{
		const std::lock_guard<std::mutex> lock(queueMutex_);
		halted_.store(true, std::memory_order_relaxed);
	}
	engine_.requestStop();
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
	} else if (type == "limits") {
		setLimits(client, text);
	} else if (type == "weights") {
		setWeights(client, text);
	} else {
		pushError(client, "unknown command: " + type, "unknown");
	}
}

// Restarts a game under the requested settings and seats.
void Session::newGame(net::ClientId client, const std::string& body) {
	const json command = parseCommand(body);

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
	spentMs_ = 0;
	searches_ = 0;
	halted_.store(false, std::memory_order_relaxed);
	pushState(client, "new_game");
	scheduleAiTurn(client);
}

// Plays one human move, then lets the AI answer.
void Session::play(net::ClientId client, const std::string& body) {
	const json command = parseCommand(body);

	int index = command.value("idx", -1);
	if (index < 0 && command.contains("x") && command.contains("y")) {
		const int x = command.value("x", -1);
		const int y = command.value("y", -1);
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
	pushState(client, "move");
	halted_.store(false, std::memory_order_relaxed);
	scheduleAiTurn(client);
}

// Runs a search without playing, and reprots the move it would pick.
void Session::suggest(net::ClientId client) {
	if (game_.status() != GameStatus::Ongoing) {
		pushError(client, legalityText(Legality::GameOver), legalityCode(Legality::GameOver));
		return;
	}
	const SearchResult result = think(client);
	lastStats_ = result.stats;
	pushState(client, "suggestion");
}

// Takes back moves until a human is to play again.
void Session::undo(net::ClientId client) {
	game_.undo();
	if (seats_.isAi(game_.toMove())) {
		game_.undo();
	}
	lastStats_ = SearchStats{};
	pushState(client, "undo");
}

// Retunes the search.
void Session::setLimits(net::ClientId client, const std::string& body) {
	const json command = parseCommand(body);
	limits_.maxDepth = std::max(1, command.value("maxDepth", limits_.maxDepth));
	limits_.budget = std::chrono::milliseconds(
		std::max(10, command.value("budgetMs", static_cast<int>(limits_.budget.count()))));
	pushState(client, "limits");
}

// Retunes the heuristic.
void Session::setWeights(net::ClientId client, const std::string& body) {
	const json command = parseCommand(body);
	EvalWeights weights = eval::weights();
	if (command.contains("weights") && command.at("weights").is_object()) {
		const json& source = command.at("weights");
		weights.five = source.value("five", weights.five);
		weights.openFour = source.value("openFour", weights.openFour);
		weights.simpleFour = source.value("simpleFour", weights.simpleFour);
		weights.openThree = source.value("openThree", weights.openThree);
		weights.brokenThree = source.value("brokenThree", weights.brokenThree);
		weights.simpleThree = source.value("simpleThree", weights.simpleThree);
		weights.openTwo = source.value("openTwo", weights.openTwo);
		weights.capturedPair = source.value("capturedPair", weights.capturedPair);
		weights.captureThreat = source.value("captureThreat", weights.captureThreat);
	}
	eval::setWeights(weights);
	pushState(client, "weights");
}

// Searches, pushing a progress event at every completed depth. The search
// clears the engine's stop flag when it starts, so a stop that came just
// before would be lost: each progress event re-arms it while halted_ is set.
SearchResult Session::think(net::ClientId client) {
	const ProgressFn onProgress = [this, client](const SearchStats& stats) {
		if (halted_.load(std::memory_order_relaxed)) {
			engine_.requestStop();
		}
		json event = statsOf(stats);
		event["type"] = "progress";
		server_.sendText(client, event.dump());
	};
	SearchLimits limits = limits_;
	limits.budget = nextBudget();
	const SearchResult result = engine_.search(game_, limits, onProgress);
	spentMs_ += result.stats.elapsedMs;
	++searches_;
	return result;
}

// Time bank: budgetMs is the average the game must stay under. A search gets
// that average times the searches so far plus one, minus what was already
// spent, kept between half the average and kBudgetCap times it.
std::chrono::milliseconds Session::nextBudget() const {
	const int64_t target = limits_.budget.count();
	const int64_t banked = target * (searches_ + 1) - spentMs_;
	return std::chrono::milliseconds(std::clamp(banked, target / 2, target * kBudgetCap));
}

// Queues one AI move if the AI is to play and nothing asked it to hold. At
// most one is ever pending.
void Session::scheduleAiTurn(net::ClientId client) {
	if (aiPending_ || halted_.load(std::memory_order_relaxed) ||
		game_.status() != GameStatus::Ongoing || !seats_.isAi(game_.toMove())) {
		return;
	}
	aiPending_ = true;
	enqueue(Job{JobKind::AiTurn, client, {}});
}

// Plays one AI move, then queues the next. Between two engines it first waits
// kAiPace so the game can be watched; a stop ends the wait and the move, any
// queued command cuts the wait short and goes first.
void Session::aiTurn(net::ClientId client) {
	aiPending_ = false;
	if (halted_.load(std::memory_order_relaxed) || game_.status() != GameStatus::Ongoing ||
		!seats_.isAi(game_.toMove())) {
		return;
	}
	if (seats_.blackIsAi && seats_.whiteIsAi) {
		std::unique_lock<std::mutex> lock(queueMutex_);
		const bool woken = queueReady_.wait_for(lock, kAiPace, [this] {
			return closing_ || !jobs_.empty() || halted_.load(std::memory_order_relaxed);
		});
		lock.unlock();
		if (woken) {
			scheduleAiTurn(client);
			return;
		}
	}
	const SearchResult result = think(client);
	lastStats_ = result.stats;
	if (result.best == kNoIdx) {
		return;
	}
	if (game_.play(result.best) != Legality::Legal) {
		pushError(client, "engine proposed an illegal move", "unknown");
		return;
	}
	pushState(client, "ai_move");
	scheduleAiTurn(client);
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
	for (const PlayedMove& move : game_.history()) {
		json captured = json::array();
		for (int k = 0; k < move.capturedCount; ++k) {
			captured.push_back(static_cast<int>(move.captured[k]));
		}
		history.push_back({{"idx", static_cast<int>(move.idx)},
						   {"player", colourName(move.player)},
						   {"captured", std::move(captured)}});
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
