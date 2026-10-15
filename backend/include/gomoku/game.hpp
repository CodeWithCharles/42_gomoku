/* -------------------------------------------------------------------------- */
/*                         Game state and arbitration.                        */
/* -------------------------------------------------------------------------- */

#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include "gomoku/board.hpp"
#include "gomoku/rules.hpp"
#include "gomoku/types.hpp"

namespace gomoku {

// Opening conventions (docs/RULES.md, section 6). Pro and LongPro restrict
// where Black's first two stones go. Swap and Swap2 let the first player lay
// out the opening, then the other one picks a colour.
enum class Opening : uint8_t { Standard, Pro, Swap, Swap2, LongPro };

// What the opening expects next: a normal move, an opening stone laid by the
// player who sets up the position, or a choice of colour.
enum class OpeningStep : uint8_t { Play, Place, Choose };

// Answer to a choice of colour: keep or take Black or White, or, in Swap2
// only, lay two more stones and hand the choice back.
enum class OpeningChoice : uint8_t { Black, White, PlaceTwo };

struct GameConfig {
	bool capturesEnabled = true;
	bool doubleThreeForbidden = true;
	bool endgameCapture = true;
	Opening opening = Opening::Standard;
};

// A choice of colour made during a Swap or Swap2 opening: how many stones
// were on the board, what was picked, whether the seats traded places, and
// the seat that chose, named by the colour it held at the start.
struct ChoiceRecord {
	std::size_t moves;
	OpeningChoice choice;
	bool swaps;
	Player actor;
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

	// Where the opening stands. Always Play under Standard, Pro and LongPro.
	OpeningStep openingStep() const;

	// Colour of the seat that must act now: the side to move during Play,
	// the player setting up the opening during Place, the chooser during
	// Choose. Seats are named by the colour they held at the start.
	Player actor() const;

	// True if the pending choice may be PlaceTwo (Swap2, first choice only).
	bool canPlaceTwo() const;

	// Applies the actor's choice during Choose. Returns OpeningRule outside a
	// choice or for an option the rule does not offer.
	Legality choose(OpeningChoice c);

	// True when the two seats now play the colour they did not start with.
	bool colorsSwapped() const;

	// Every choice of colour made so far, oldest first.
	const std::vector<ChoiceRecord>& choices() const;

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
	std::vector<ChoiceRecord> choices_{};
};

}  // namespace gomoku
