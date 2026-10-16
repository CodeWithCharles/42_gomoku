/* -------------------------------------------------------------------------- */
/*                                    Game                                    */
/* -------------------------------------------------------------------------- */

#include "gomoku/game.hpp"

#include <algorithm>

namespace gomoku {
namespace {

// Smallest distance from the centre allowed for Black's second stone, or 0
// when the opening sets no such ring.
int proRing(Opening opening) {
	switch (opening) {
		case Opening::Pro:
			return 3;
		case Opening::LongPro:
			return 4;
		default:
			return 0;
	}
}

// True if a stone on `i` respects the Pro family rules after `played` moves:
// Black opens on the centre, and Black's second stone, the third move of the
// game, lies at least `ring` intersections away from it.
bool proOpeningAllows(Opening opening, size_t played, Idx i) {
	const int ring = proRing(opening);
	if (ring == 0) {
		return true;
	}
	const int centre = kBoardSize / 2;
	if (played == 0) {
		return i == idxOf(centre, centre);
	}
	if (played == 2) {
		const int dx = xOf(i) - centre;
		const int dy = yOf(i) - centre;
		const int distance = std::max(dx < 0 ? -dx : dx, dy < 0 ? -dy : dy);
		return distance >= ring;
	}
	return true;
}

}  // namespace

Game::Game(GameConfig cfg) : config_(cfg) {}

// Restarts a game under new settings.
void Game::reset(GameConfig cfg) {
	config_ = cfg;
	board_.clear();
	toMove_ = Player::Black;
	status_ = GameStatus::Ongoing;
	winReason_ = WinReason::None;
	pairs_ = {0, 0};
	history_.clear();
	choices_.clear();
}

// Restarts a game keeping the current settings.
void Game::reset() {
	reset(config_);
}

// The board as it stands.
const Board& Game::board() const {
	return board_;
}

// Colour whose turn it is.
Player Game::toMove() const {
	return toMove_;
}

// Ongoing, or how the game ended.
GameStatus Game::status() const {
	return status_;
}

// Why the game ended, meaningless while it is ongoing.
WinReason Game::winReason() const {
	return winReason_;
}

const GameConfig& Game::config() const {
	return config_;
}

// Arbitrated moves played so far, oldest first.
const std::vector<PlayedMove>& Game::history() const {
	return history_;
}

// Pairs `p` has captured from the opponent.
int Game::pairsTaken(Player p) const {
	return pairs_[playerIndex(p)];
}

// Pairs captured by both players, indexed by playerIndex.
std::array<int, 2> Game::pairs() const {
	return pairs_;
}

// Tells whether `p` may play on `i`, without touching the game state.
Legality Game::check(Idx i, Player p) const {
	if (status_ != GameStatus::Ongoing) {
		return Legality::GameOver;
	}
	if (p != toMove_) {
		return Legality::NotYourTurn;
	}
	if (i < 0 || i >= kCellCount) {
		return Legality::OutOfBounds;
	}
	if (!board_.isEmpty(i)) {
		return Legality::Occupied;
	}
	if (openingStep() == OpeningStep::Choose ||
		!proOpeningAllows(config_.opening, history_.size(), i)) {
		return Legality::OpeningRule;
	}

	Board probe = board_;
	probe.place(i, p);

	// The subject is explicit: creating a double three BY CAPTURING is allowed.
	// So captures are tested first, and a capturing move leaves right away.
	if (config_.capturesEnabled) {
		PlayedMove ignored;
		if (rules::findCaptures(probe, i, p, ignored) > 0) {
			return Legality::Legal;
		}
	}
	probe.remove(i);

	if (config_.doubleThreeForbidden && rules::createsDoubleThree(probe, i, p)) {
		return Legality::DoubleThree;
	}
	return Legality::Legal;
}

// Plays an arbitrated move.
Legality Game::play(Idx i) {
	const Legality verdict = check(i, toMove_);
	if (verdict != Legality::Legal) {
		return verdict;
	}
	PlayedMove move;
	makeMove(i, move);
	history_.push_back(move);
	updateStatus(move);
	return Legality::Legal;
}

// Applies a move with no check and passes the turn.
void Game::makeMove(Idx i, PlayedMove& undo) {
	undo.idx = i;
	undo.player = toMove_;
	undo.capturedCount = 0;

	board_.place(i, toMove_);
	if (config_.capturesEnabled) {
		const int taken = rules::findCaptures(board_, i, toMove_, undo);
		for (int k = 0; k < undo.capturedCount; ++k) {
			board_.remove(undo.captured[k]);
		}
		pairs_[playerIndex(toMove_)] += taken;
	}
	toMove_ = opponent(toMove_);
}

// Rolls a move back, stones and counters included.
void Game::unmakeMove(const PlayedMove& undo) {
	toMove_ = undo.player;
	const Player foe = opponent(undo.player);
	for (int k = 0; k < undo.capturedCount; ++k) {
		board_.place(undo.captured[k], foe);
	}
	pairs_[playerIndex(undo.player)] -= undo.capturedCount / 2;
	board_.remove(undo.idx);
}

// End of game as seen from the search, right after makeMove.
//
// Deliberately cheap: it only looks at the stone just played, where
// updateStatus scans the whole board. It therefore misses the case where the
// OPPONENT holds a five left standing from an earlier move. Module 2 will have
// to carry that state explicitly if the search needs it.
GameStatus Game::terminalAfter(const PlayedMove& last) const {
	const Player mover = last.player;
	if (pairs_[playerIndex(mover)] >= kPairsToWin) {
		return mover == Player::Black ? GameStatus::BlackWins : GameStatus::WhiteWins;
	}
	if (board_.hasFive(Player::Black) || board_.hasFive(Player::White)) {
		WinReason why = WinReason::None;
		const GameStatus foeOutcome = alignmentOutcome(opponent(mover), why);
		if (foeOutcome != GameStatus::Ongoing) {
			return foeOutcome;
		}
		const GameStatus moverOutcome = alignmentOutcome(mover, why);
		if (moverOutcome != GameStatus::Ongoing) {
			return moverOutcome;
		}
	}
	if (board_.isFull()) {
		return GameStatus::Draw;
	}
	return GameStatus::Ongoing;
}

// Records the winner and why
void Game::declareWinner(Player p, WinReason why) {
	status_ = p == Player::Black ? GameStatus::BlackWins : GameStatus::WhiteWins;
	winReason_ = why;
}

// Settles a five of `p` under the endgame capture rule. Returns true when the
// game is decided, which may be in favour of the opponent.
bool Game::resolveAlignment(Player p) {
	WinReason why = WinReason::None;
	const GameStatus outcome = alignmentOutcome(p, why);
	if (outcome == GameStatus::Ongoing) {
		return false;
	}
	status_ = outcome;
	winReason_ = why;
	return true;
}

// Outcome of the fives `p` has on the board: Ongoing when there is none or
// the opponent can still break them by capture, otherwise who wins and why.
// Breaking needs captures to be on as well as the endgame capture rule.
GameStatus Game::alignmentOutcome(Player p, WinReason& why) const {
	if (!board_.hasFive(p)) {
		return GameStatus::Ongoing;
	}
	const Idx anchor = rules::findAnyAlignment(board_, p);
	const Player foe = opponent(p);
	if (config_.endgameCapture && config_.capturesEnabled) {
		if (rules::canBreakAlignment(board_, p, anchor, foe)) {
			return GameStatus::Ongoing;
		}
		if (pairs_[playerIndex(foe)] == kPairsToWin - 1 &&
			rules::canCaptureAnyPair(board_, p, foe)) {
			why = WinReason::Captures;
			return foe == Player::Black ? GameStatus::BlackWins : GameStatus::WhiteWins;
		}
	}
	why = WinReason::Alignment;
	return p == Player::Black ? GameStatus::BlackWins : GameStatus::WhiteWins;
}

// Settles the game after an arbitrated move.
void Game::updateStatus(const PlayedMove& last) {
	const Player mover = last.player;
	const Player foe = opponent(mover);

	if (pairs_[playerIndex(mover)] >= kPairsToWin) {
		declareWinner(mover, WinReason::Captures);
		return;
	}

	// The opponent may have left a five standing last turn. If this move did
	// not break it, it wins now, so it is settled before the mover's own.
	if (resolveAlignment(foe)) {
		return;
	}
	if (resolveAlignment(mover)) {
		return;
	}

	if (board_.isFull()) {
		status_ = GameStatus::Draw;
		winReason_ = WinReason::BoardFull;
	}
}

// Takes back the last arbitrated move.
bool Game::undo() {
	if (history_.empty()) {
		return false;
	}
	const PlayedMove last = history_.back();
	history_.pop_back();
	unmakeMove(last);
	status_ = GameStatus::Ongoing;
	winReason_ = WinReason::None;
	while (!choices_.empty() && choices_.back().moves > history_.size()) {
		choices_.pop_back();
	}
	return true;
}

// Where the opening stands. Swap: three stones laid by the first player, then
// a choice by the second. Swap2: the second player may instead lay two more
// stones, and the first player then chooses.
OpeningStep Game::openingStep() const {
	const size_t played = history_.size();
	if (config_.opening == Opening::Swap) {
		if (played < 3) {
			return OpeningStep::Place;
		}
		return choices_.empty() ? OpeningStep::Choose : OpeningStep::Play;
	}
	if (config_.opening == Opening::Swap2) {
		if (played < 3) {
			return OpeningStep::Place;
		}
		if (choices_.empty()) {
			return OpeningStep::Choose;
		}
		if (choices_.front().choice != OpeningChoice::PlaceTwo) {
			return OpeningStep::Play;
		}
		if (played < 5) {
			return OpeningStep::Place;
		}
		return choices_.size() < 2 ? OpeningStep::Choose : OpeningStep::Play;
	}
	return OpeningStep::Play;
}

// Colour of the seat that acts now. During the opening no swap has happened
// yet, so the first player is the Black seat and the second the White seat.
Player Game::actor() const {
	switch (openingStep()) {
		case OpeningStep::Place:
			return history_.size() < 3 ? Player::Black : Player::White;
		case OpeningStep::Choose:
			return history_.size() < 5 ? Player::White : Player::Black;
		case OpeningStep::Play:
			break;
	}
	return toMove_;
}

// True if the pending choice may be PlaceTwo.
bool Game::canPlaceTwo() const {
	return config_.opening == Opening::Swap2 && choices_.empty() &&
		   openingStep() == OpeningStep::Choose;
}

// Records the actor's choice. Picking the colour the other seat holds swaps
// the seats; PlaceTwo swaps nothing and hands the next two stones over.
Legality Game::choose(OpeningChoice c) {
	if (status_ != GameStatus::Ongoing || openingStep() != OpeningStep::Choose) {
		return Legality::OpeningRule;
	}
	if (c == OpeningChoice::PlaceTwo && !canPlaceTwo()) {
		return Legality::OpeningRule;
	}
	const Player picked = c == OpeningChoice::Black ? Player::Black : Player::White;
	const bool swaps = c != OpeningChoice::PlaceTwo && picked != actor();
	choices_.push_back(ChoiceRecord{history_.size(), c, swaps, actor()});
	return Legality::Legal;
}

// Every choice of colour made so far, oldest first.
const std::vector<ChoiceRecord>& Game::choices() const {
	return choices_;
}

// True when an odd number of choices swapped the seats.
bool Game::colorsSwapped() const {
	bool swapped = false;
	for (const ChoiceRecord& record : choices_) {
		swapped = swapped != record.swaps;
	}
	return swapped;
}

}  // namespace gomoku
