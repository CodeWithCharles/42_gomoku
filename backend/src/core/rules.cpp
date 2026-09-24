/* -------------------------------------------------------------------------- */
/*                                 Rules impl                                 */
/* -------------------------------------------------------------------------- */

#include "gomoku/rules.hpp"

#include <array>

namespace gomoku::rules {
namespace {

// The 8 directions. Alignments are symmetric and use kAxes, but captures are
// not: X O O X reads one way only, so they need all 8.
constexpr std::array<Delta, 8> kDirs = {{
	{1, 0},
	{-1, 0},
	{0, 1},
	{0, -1},
	{1, 1},
	{-1, -1},
	{1, -1},
	{-1, 1},
}};

// True if (x, y) is on the board and holds a stone of `p`.
bool cellIs(const Board& b, int x, int y, Player p) {
	return inBounds(x, y) && b.at(x, y) == p;
}

// True if (x, y) is on the board and free.
bool cellEmpty(const Board& b, int x, int y) {
	return inBounds(x, y) && b.at(x, y) == Player::None;
}

// True if the run of `p` crossing `idx` along `axis` is exactly four stones
// long with both ends free, which is the definition of an open four.
bool isOpenFour(const Board& b, Idx idx, int axis, Player p) {
	const Delta d = kAxes[axis];
	int startX = xOf(idx);
	int startY = yOf(idx);
	while (cellIs(b, startX - d.dx, startY - d.dy, p)) {
		startX -= d.dx;
		startY -= d.dy;
	}
	int length = 0;
	int x = startX;
	int y = startY;
	while (cellIs(b, x, y, p)) {
		++length;
		x += d.dx;
		y += d.dy;
	}
	if (length != 4) {
		return false;
	}
	return cellEmpty(b, x, y) && cellEmpty(b, startX - d.dx, startY - d.dy);
}

// True if `p` owns a free three on `axis` through `i`, the stone on `i` being
// already placed. Applies the subject definition literally: there is a free
// three if some empty cell, once played, would produce an open four holding
// both `i` and that cell. Not recursive, see docs/RULES.md.
bool axisHasFreeThree(Board& b, Idx i, int axis, Player p) {
	const Delta d = kAxes[axis];
	for (int k = -kWindowRadius; k <= kWindowRadius; ++k) {
		if (k == 0) {
			continue;
		}
		const int x = xOf(i) + k * d.dx;
		const int y = yOf(i) + k * d.dy;
		if (!inBounds(x, y)) {
			continue;
		}
		const Idx candidate = idxOf(x, y);
		if (b.at(candidate) != Player::None) {
			continue;
		}
		b.place(candidate, p);
		const bool found = isOpenFour(b, i, axis, p) && isOpenFour(b, candidate, axis, p);
		b.remove(candidate);
		if (found) {
			return true;
		}
	}
	return false;
}

// True if `foe` can capture, in one move, a pair of `victim` holding `s`.
bool pairAroundIsCapturable(const Board& b, Idx s, Player victim, Player foe) {
	const int sx = xOf(s);
	const int sy = yOf(s);
	for (const Delta& d : kDirs) {
		if (!cellIs(b, sx + d.dx, sy + d.dy, victim)) {
			continue;
		}
		if (!cellIs(b, sx - d.dx, sy - d.dy, foe)) {
			continue;
		}
		if (!cellEmpty(b, sx + 2 * d.dx, sy + 2 * d.dy)) {
			continue;
		}
		return true;
	}
	return false;
}

}  // namespace

int findCaptures(const Board& b, Idx i, Player p, PlayedMove& out) {
	const Player foe = opponent(p);
	const int cx = xOf(i);
	const int cy = yOf(i);
	int pairs = 0;
	out.capturedCount = 0;
	for (const Delta& d : kDirs) {
		if (!cellIs(b, cx + d.dx, cy + d.dy, foe)) {
			continue;
		}
		if (!cellIs(b, cx + 2 * d.dx, cy + 2 * d.dy, foe)) {
			continue;
		}
		if (!cellIs(b, cx + 3 * d.dx, cy + 3 * d.dy, p)) {
			continue;
		}
		out.captured[out.capturedCount++] = idxOf(cx + d.dx, cy + d.dy);
		out.captured[out.capturedCount++] = idxOf(cx + 2 * d.dx, cy + 2 * d.dy);
		++pairs;
	}
	return pairs;
}

// True if putting a stone of `p` on the empty cell `i` creates two free threes.
bool createsDoubleThree(Board& b, Idx i, Player p) {
	b.place(i, p);
	int threes = 0;
	for (int axis = 0; axis < 4 && threes < 2; ++axis) {
		if (axisHasFreeThree(b, i, axis, p)) {
			++threes;
		}
	}
	b.remove(i);
	return threes >= 2;
}

// True if `p` owns a run of kAlignToWin or more crossing `i`.
bool hasAlignment(const Board& b, Idx i, Player p) {
	for (int axis = 0; axis < 4; ++axis) {
		if (b.countLine(i, axis, p) >= kAlignToWin) {
			return true;
		}
	}
	return false;
}

// Looks for a winning run of `p` anywhere on the board.
Idx findAnyAlignment(const Board& b, Player p) {
	for (Idx i = 0; i < kCellCount; ++i) {
		if (b.at(i) != p) {
			continue;
		}
		if (hasAlignment(b, i, p)) {
			return i;
		}
	}
	return kNoIdx;
}

// True if `foe` can break the run of `p` crossing `alignAnchor` by capture.
bool canBreakAlignment(const Board& b, Player p, Idx alignAnchor, Player foe) {
	if (alignAnchor == kNoIdx) {
		return false;
	}
	for (int axis = 0; axis < 4; ++axis) {
		if (b.countLine(alignAnchor, axis, p) < kAlignToWin) {
			continue;
		}
		const Delta d = kAxes[axis];
		int x = xOf(alignAnchor);
		int y = yOf(alignAnchor);
		while (cellIs(b, x - d.dx, y - d.dy, p)) {
			x -= d.dx;
			y -= d.dy;
		}
		while (cellIs(b, x, y, p)) {
			if (pairAroundIsCapturable(b, idxOf(x, y), p, foe)) {
				return true;
			}
			x += d.dx;
			y += d.dy;
		}
	}
	return false;
}

// True if `foe` can capture at least one pair of `victim` in a single move.
bool canCaptureAnyPair(const Board& b, Player victim, Player foe) {
	for (Idx i = 0; i < kCellCount; ++i) {
		if (b.at(i) != victim) {
			continue;
		}
		if (pairAroundIsCapturable(b, i, victim, foe)) {
			return true;
		}
	}
	return false;
}

}  // namespace gomoku::rules
