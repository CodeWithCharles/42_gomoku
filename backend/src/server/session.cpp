/* -------------------------------------------------------------------------- */
/*                                   Session                                  */
/* -------------------------------------------------------------------------- */

#include "server/session.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <vector>

namespace gomoku::server {
namespace {

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

// Wires the connection and message handlers onto the server.
void Session::install() {
	server_.onConnect([this](net::ClientId client) { onConnect(client); });
	server_.onText([this](net::ClientId client, const std::string& text) { onText(client, text); });
}

// A client just finished the handshake: hand it the full state.
void Session::onConnect(net::ClientId client) {
	const std::lock_guard<std::mutex> lock(mutex_);
	pushState(client, "connected");
}

// Dispatches one command. nothing may escape from here: an exception crossing
// a civetweb callback would take the whole engine down.
void Session::onText(net::ClientId client, const std::string& text) {
	try {
		const json command = parseCommand(text);
		if (command.is_discarded() || !command.is_object()) {
			const std::lock_guard<std::mutex> lock(mutex_);
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
		} else if (type == "stop") {
			engine_.requestStop();
		} else {
			const std::lock_guard<std::mutex> lock(mutex_);
			pushError(client, "unknown command: " + type, "unknown");
		}
	} catch (const std::exception& error) {
		const std::lock_guard<std::mutex> lock(mutex_);
		pushError(client, error.what(), "unknown");
	} catch (...) {
		const std::lock_guard<std::mutex> lock(mutex_);
		pushError(client, "internal error", "unknown");
	}
}

// Restarts a game under the requested settings and seats.
void Session::newGame(net::ClientId client, const std::string& body) {
	const std::lock_guard<std::mutex> lock(mutex_);
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
	pushState(client, "new_game");
	runAiTurns(client);
}

// Plays one human move, then lets the AI answer.
void Session::play(net::ClientId client, const std::string& body) {
	const std::lock_guard<std::mutex> lock(mutex_);
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
	runAiTurns(client);
}

// Runs a search without playing, and reprots the move it would pick.
void Session::suggest(net::ClientId client) {
	const std::lock_guard<std::mutex> lock(mutex_);
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
	const std::lock_guard<std::mutex> lock(mutex_);
	game_.undo();
	if (seats_.isAi(game_.toMove())) {
		game_.undo();
	}
	lastStats_ = SearchStats{};
	pushState(client, "undo");
}

// Retunes the search.
void Session::setLimits(net::ClientId client, const std::string& body) {
	const std::lock_guard<std::mutex> lock(mutex_);
	const json command = parseCommand(body);
	limits_.maxDepth = std::max(1, command.value("maxDepth", limits_.maxDepth));
	limits_.budget = std::chrono::milliseconds(
		std::max(10, command.value("budgetMs", static_cast<int>(limits_.budget.count()))));
	pushState(client, "limits");
}

// Retunes the heuristic.
void Session::setWeights(net::ClientId client, const std::string& body) {
	const std::lock_guard<std::mutex> lock(mutex_);
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

// Searches, pushing a progress event at every completed depth.
SearchResult Session::think(net::ClientId client) {
	const ProgressFn onProgress = [this, client](const SearchStats& stats) {
		json event = statsOf(stats);
		event["type"] = "progress";
		server_.sendText(client, event.dump());
	};
	return engine_.search(game_, limits_, onProgress);
}

void Session::runAiTurns(net::ClientId client) {
	for (int guard = 0; guard < kCellCount; ++guard) {
		if (game_.status() != GameStatus::Ongoing || !seats_.isAi(game_.toMove())) {
			return;
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
	}
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
