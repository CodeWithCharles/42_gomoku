/* -------------------------------------------------------------------------- */
/*                                    Game                                    */
/* -------------------------------------------------------------------------- */

#include "gomoku/game.hpp"

namespace gomoku {

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
	const GameStatus moverWins =
		mover == Player::Black ? GameStatus::BlackWins : GameStatus::WhiteWins;

	if (pairs_[playerIndex(mover)] >= kPairsToWin) {
		return moverWins;
	}
	if (rules::hasAlignment(board_, last.idx, mover)) {
		if (!config_.endgameCapture) {
			return moverWins;
		}
		if (!rules::canBreakAlignment(board_, mover, last.idx, opponent(mover))) {
			return moverWins;
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
	const Idx anchor = rules::findAnyAlignment(board_, p);
	if (anchor == kNoIdx) {
		return false;
	}
	const Player foe = opponent(p);
	if (config_.endgameCapture) {
		if (rules::canBreakAlignment(board_, p, anchor, foe)) {
			return false;
		}
		if (pairs_[playerIndex(foe)] == kPairsToWin - 1 &&
			rules::canCaptureAnyPair(board_, p, foe)) {
			declareWinner(foe, WinReason::Captures);
			return true;
		}
	}
	declareWinner(p, WinReason::Alignment);
	return true;
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
	return true;
}

}  // namespace gomoku
