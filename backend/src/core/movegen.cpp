/* -------------------------------------------------------------------------- */
/*                                   Movegen                                  */
/* -------------------------------------------------------------------------- */

#include "gomoku/movegen.hpp"

namespace gomoku::movegen {

// Fills `out` with the empty cells sitting within `radius` of a stone.
void generate(const Board& b, MoveList& out, int radius) {
	out.count = 0;
	if (b.stoneCount() == 0) {
		out.moves[out.count++] = idxOf(kBoardSize / 2, kBoardSize / 2);
		return;
	}
	for (Idx i = 0; i < kCellCount; ++i) {
		if (!b.isEmpty(i)) {
			continue;
		}
		if (b.hasNeighbor(i, radius)) {
			out.moves[out.count++] = i;
		}
	}
}

}  // namespace gomoku::movegen
