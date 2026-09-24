/* -------------------------------------------------------------------------- */
/*                 Module 1 (Generation) / Module 2 (ordering)                */
/* -------------------------------------------------------------------------- */

#pragma once

#include <array>

#include "gomoku/board.hpp"
#include "gomoku/types.hpp"

namespace gomoku {

inline constexpr int kNeighborRadius = 2;

// Candidate moves. A plain aggregate sized for the worst case: generation sits
// in the hot loop and must never allocate, and direct member access costs
// nothing.
struct MoveList {
	std::array<Idx, kCellCount> moves{};
	int count = 0;
};

namespace movegen {

// Fills `out` with the pseudo legal moves: empty cells close to a stone. Does
// NOT filter double threes, which are checked at the root only.
// An empty board yields the centre alone.
void generate(const Board& b, MoveList& out, int radius = kNeighborRadius);

}  // namespace movegen
}  // namespace gomoku
