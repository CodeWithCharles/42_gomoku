/* -------------------------------------------------------------------------- */
/*                            Module 3 (Heuristic)                            */
/* -------------------------------------------------------------------------- */

//  Contract with the search (module 2) :
//    - evaluate() scores from the point of view of `toMove` (negamax) ;
//    - it must be FAST: it runs hundreds of thousands of times per move ;
//    - it must never return +-kScoreWin, reserved for real terminal states.
//
//  Recommended path: a pattern table indexed by LineWindow (18 bits, 262144
//  entries) built at compile time the way board.cpp builds its Zobrist table.
//  A move only touches 4 axes times 9 windows, so the evaluation can become
//  incremental instead of rescanning the board.

#include <array>

#include "gomoku/board.hpp"
#include "gomoku/types.hpp"

namespace gomoku {

// Exposed so the debug panel can tune them live (see docs/PROTOCOL.md).
struct EvalWeights {
	Score five = 100000;
	Score openFour = 20000;
	Score simpleFour = 4000;
	Score openThree = 2000;
	Score brokenThree = 1200;
	Score simpleThree = 300;
	Score openTwo = 100;
	Score capturedPair = 1500;
	Score captureThreat = 400;
};

namespace eval {

const EvalWeights& weights();
void setWeights(const EvalWeights& w);

// Static evaluation. `pairs` holds the pairs captured by each player, indexed
// by playerIndex.
Score evaluate(const Board& b, Player toMove, const std::array<int, 2>& pairs);

// Static score of a candidate move, used for move ordering ONLY. It never
// feeds the evaluation itself.
Score moveScore(const Board& b, Idx i, Player p);

}  // namespace eval
}  // namespace gomoku
