/* -------------------------------------------------------------------------- */
/*                              Rules, 42 flavor                              */
/* -------------------------------------------------------------------------- */

//  1. CAPTURE : a stone that flanks EXACTLY two enemy stones (pattern X O O X)
//     removes that pair. Never one stone, never three. Moving in between two
//     enemy stones is safe : the flanking player has to play the last stone.
//
//  2. DOUBLE THREE : a move creating two free threes is forbidden. A free
//     three is a three that, left alone, yields an open four. Creating a
//     double three BY CAPTURING is explicitly allowed by the subject.
//
//  3. ENDGAME CAPTURE : five in a row only wins if the opponent cannot break
//     it by capturing one of its pairs. And if its owner already lost four
//     pairs and the opponent can take a fifth, the opponent wins instead.

#include "gomoku/board.hpp"
#include "gomoku/types.hpp"

namespace gomoku::rules {

// Pairs captured by `p` playing on `i`. The stone on `i` must ALREADY be on
// the board. Fills out.captured and out.capturedCount, and returns the number
// of PAIRS in [0, 4], so capturedCount is twice the returned value.
int findCaptures(const Board& b, Idx i, Player p, PlayedMove& out);

// True if putting a stone of `p` on `i` creates at least two free threes.
// `i` must be EMPTY on entry: the move is simulated then undone, so `b` comes
// back exactly as it was.
bool createsDoubleThree(Board& b, Idx i, Player p);

// True if `p` owns a run of kAlignToWin or more crossing `i`.
bool hasAlignment(const Board& b, Idx i, Player p);

// Looks for a winning run of `p` anywhere on the board. Returns one stone of
// that run, or kNoIdx. Scans the whole board, so keep it off the hot path.
Idx findAnyAlignment(const Board& b, Player p);

// ENDGAME CAPTURE : can `foe` break the run of `p` crossing `alignAnchor` by
// capturing one of its pairs in a single move?
bool canBreakAlignment(const Board& b, Player p, Idx alignAnchor, Player foe);

// Can `foe` capture at least one pair of `victim` in a single move? Backs the
// "already lost four pairs and one more is available" branch of the rule.
bool canCaptureAnyPair(const Board& b, Player victim, Player foe);

}  // namespace gomoku::rules
