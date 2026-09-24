/* -------------------------------------------------------------------------- */
/*                         Game state and arbitration.                        */
/* -------------------------------------------------------------------------- */

#pragma once

#include <array>
#include <vector>

#include "gomoku/board.hpp"
#include "gomoku/rules.hpp"
#include "gomoku/types.hpp"

namespace gomoku {

// Opening conventions. Only Standard is implemented; the others are reserved
// for the bonus part and must not be relied on yet.
enum class Opening : uint8_t { Standard, Pro, Swap, Swap2 };

struct GameConfig {
	bool capturesEnabled = true;
	bool doubleThreeForbidden = true;
	bool endgameCapture = true;
	Opening opening = Opening::Standard;
};

class Game {
  public:
	explicit Game(GameConfig cfg = {});

	void reset(GameConfig cfg);
	void reset();

	const Board& board() const;
	Player toMove() const;
	GameStatus status() const;
	WinReason winReason() const;
	const GameConfig& config() const;
	const std::vector<PlayedMove>& history() const;
	int pairsTaken(Player p) const;
	std::array<int, 2> pairs() const;

	// Tells whether `p` may play on `i`, without touching the game state.
	Legality check(Idx i, Player p) const;

	// Plays an arbitrated mvoe. Returns Legality::Legal when it was applied,
	// and the reason for the refusal otherwise.
	Legality play(Idx i);

	// Searh primitives: no arbitration, no history, no allocation
	// makeMove assumes the move was already vetted, and passes the turn.
	void makeMove(Idx i, PlayedMove& undo);
	void unmakeMove(const PlayedMove& undo);

	// End of game as seen from the search, to call right after makeMove.
	// Cheap on purpose: it only looks at the stone just played, so it misses
	// the "pending five" case that updateStatus resolves. See game.cpp.
	GameStatus terminalAfter(const PlayedMove& last) const;

	// Takes back the last arbitrated move. Used by the UI undo button.
	bool undo();

  private:
	void updateStatus(const PlayedMove& last);
	bool resolveAlignment(Player p);
	void declareWinner(Player p, WinReason why);

	GameConfig config_{};
	Board board_{};
	Player toMove_ = Player::Black;
	GameStatus status_ = GameStatus::Ongoing;
	WinReason winReason_ = WinReason::None;
	std::array<int, 2> pairs_{};
	std::vector<PlayedMove> history_{};
};

}  // namespace gomoku
