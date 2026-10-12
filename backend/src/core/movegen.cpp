/* -------------------------------------------------------------------------- */
/*                                   Movegen                                  */
/* -------------------------------------------------------------------------- */

#include "gomoku/movegen.hpp"

namespace gomoku::movegen {

// Fills `out` with the empty cells sitting within `radius` of a stone. Marks
// the squares around each stone, then collects them in index order.
void generate(const Board& b, MoveList& out, int radius) {
	out.count = 0;
	if (b.stoneCount() == 0) {
		out.moves[out.count++] = idxOf(kBoardSize / 2, kBoardSize / 2);
		return;
	}
	std::array<bool, kCellCount> near{};
	for (Idx i = 0; i < kCellCount; ++i) {
		if (b.isEmpty(i)) {
			continue;
		}
		const int cx = xOf(i);
		const int cy = yOf(i);
		for (int y = cy - radius; y <= cy + radius; ++y) {
			for (int x = cx - radius; x <= cx + radius; ++x) {
				if (inBounds(x, y)) {
					near[idxOf(x, y)] = true;
				}
			}
		}
	}
	for (Idx i = 0; i < kCellCount; ++i) {
		if (near[i] && b.isEmpty(i)) {
			out.moves[out.count++] = i;
		}
	}
}

}  // namespace gomoku::movegen
