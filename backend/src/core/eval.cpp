/* -------------------------------------------------------------------------- */
/*         Reference implementation - deliberately naive and readable         */
/* -------------------------------------------------------------------------- */

//  It is CORRECT but SLOW: it rescans the whole board on every call, where the
//  search makes hundreds of thousands of them per move. This is module 3's
//  starting point, not the version to hand in.
//
//  What replaces it :
//    1. a pattern table indexed by LineWindow, built at compile time ;
//    2. incremental evaluation, since a move only touches 4 axes ;
//    3. threat detection exposed to the search to cut the branching factor.

#include "gomoku/eval.hpp"

#include <cstdlib>

namespace gomoku::eval {
namespace {

EvalWeights gWeights{};

// Score of a run of `length` stones with `openEnds` free extremities.
Score runValue(int length, int openEnds, const EvalWeights& w) {
	if (length >= kAlignToWin) {
		return w.five;
	}
	if (openEnds == 0) {
		return 0;
	}
	switch (length) {
		case 4:
			return openEnds == 2 ? w.openFour : w.simpleFour;
		case 3:
			return openEnds == 2 ? w.openThree : w.simpleThree;
		case 2:
			return openEnds == 2 ? w.openTwo : 0;
		default:
			return 0;
	}
}

// True if (x, y) is on the board and free.
bool isEmptyAt(const Board& b, int x, int y) {
	return inBounds(x, y) && b.at(x, y) == Player::None;
}

// Sums the value of every run owned by `p`, counting each rune once.
Score sideScore(const Board& b, Player p, const EvalWeights& w) {
	Score total = 0;
	for (Idx i = 0; i < kCellCount; ++i) {
		if (b.at(i) != p) {
			continue;
		}
		for (int axis = 0; axis < 4; ++axis) {
			const Delta d = kAxes[axis];
			const int px = xOf(i) - d.dx;
			const int py = yOf(i) - d.dy;
			if (inBounds(px, py) && b.at(px, py) == p) {
				continue;
			}
			int length = 0;
			int x = xOf(i);
			int y = yOf(i);
			while (inBounds(x, y) && b.at(x, y) == p) {
				++length;
				x += d.dx;
				y += d.dy;
			}
			const int openEnds = (isEmptyAt(b, px, py) ? 1 : 0) + (isEmptyAt(b, x, y) ? 1 : 0);
			total += runValue(length, openEnds, w);
		}
	}
	return total;
}

}  // namespace

// Current heuristic weights.
const EvalWeights& weights() {
	return gWeights;
}

// Replaces the heuristic weights.
void setWeights(const EvalWeights& w) {
	gWeights = w;
}

// Static evaluation from the point of view of `toMove`.
Score evaluate(const Board& b, Player toMove, const std::array<int, 2>& pairs) {
	const EvalWeights& w = gWeights;
	const Score black =
		sideScore(b, Player::Black, w) + pairs[playerIndex(Player::Black)] * w.capturedPair;
	const Score white =
		sideScore(b, Player::White, w) + pairs[playerIndex(Player::White)] * w.capturedPair;
	const Score diff = black - white;
	return toMove == Player::Black ? diff : -diff;
}

// Static score of a candidate move, for ordering only.
Score moveScore(const Board& b, Idx i, Player p) {
	const Player foe = opponent(p);
	Score score = 0;
	for (int axis = 0; axis < 4; ++axis) {
		const Delta d = kAxes[axis];
		int own = 0;
		int block = 0;
		for (int sign = -1; sign <= 1; sign += 2) {
			int x = xOf(i) + sign * d.dx;
			int y = yOf(i) + sign * d.dy;
			while (inBounds(x, y) && b.at(x, y) == p) {
				++own;
				x += sign * d.dx;
				y += sign * d.dy;
			}
			x = xOf(i) + sign * d.dx;
			y = yOf(i) + sign * d.dy;
			while (inBounds(x, y) && b.at(x, y) == foe) {
				++block;
				x += sign * d.dx;
				y += sign * d.dy;
			}
		}
		score += own * own * 16 + block * block * 12;
	}
	score += 18 - std::abs(xOf(i) - kBoardSize / 2) - std::abs(yOf(i) - kBoardSize / 2);
	return score;
}

}  // namespace gomoku::eval
