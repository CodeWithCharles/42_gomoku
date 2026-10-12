/* -------------------------------------------------------------------------- */
/*                          Pattern based evaluation                          */
/* -------------------------------------------------------------------------- */

// Every stone reads its shape on each axis from a table indexed by the 8
// cells around it (16 bits, the centre being always Own). The table is built
// once by levels: fours, then threes (one move from an open four, the
// subject's free three), then twos (one move from a three).

#include "gomoku/eval.hpp"

#include <algorithm>
#include <bit>

namespace gomoku::eval {
namespace {

enum Shape : uint8_t {
	kNone,
	kTwo,
	kSimpleThree,
	kBrokenThree,
	kOpenThree,
	kFour,
	kOpenFour,
	kFive,
	kShapeCount,
};

inline constexpr int kPatternCount = 1 << 16;
inline constexpr uint8_t kShapeMask = 0x0F;
inline constexpr int kCaptureShift = 4;
inline constexpr int kTakeShift = 6;
inline constexpr uint8_t kCountMask = 0x03;
inline constexpr Score kDecisive = kScoreWin / 2;

using Cells = std::array<CellCode, kWindowCells>;
using PatternTable = std::array<uint8_t, kPatternCount>;

// Decodes the 8 cells of a pattern index around an Own centre.
Cells decode(int index) {
	Cells c{};
	for (int k = 0, slot = 0; k < kWindowCells; ++k) {
		if (k == kWindowRadius) {
			c[k] = CellCode::Own;
			continue;
		}
		c[k] = static_cast<CellCode>((index >> (2 * slot)) & 3);
		++slot;
	}
	return c;
}

// Pattern index after an own stone lands on the empty cell `k` (0..8).
int withOwn(int index, int k) {
	const int slot = k < kWindowRadius ? k : k - 1;
	return index | (static_cast<int>(CellCode::Own) << (2 * slot));
}

// 16 bit pattern index of a window, dropping the centre cell.
int indexOf(LineWindow w) {
	return static_cast<int>((w & 0xFFU) | ((w >> 10) << 8));
}

// The same pattern seen from the other side: Own and Foe swap, Empty and
// Wall stay, which is swapping the two bits of every cell.
int mirrored(int index) {
	return ((index & 0x5555) << 1) | ((index & 0xAAAA) >> 1);
}

// Length of the own run crossing the centre; `lo` and `hi` receive its ends.
int runThroughCentre(const Cells& c, int& lo, int& hi) {
	lo = kWindowRadius;
	while (lo > 0 && c[lo - 1] == CellCode::Own) {
		--lo;
	}
	hi = kWindowRadius;
	while (hi < kWindowCells - 1 && c[hi + 1] == CellCode::Own) {
		++hi;
	}
	return hi - lo + 1;
}

// True if the centre stone belongs to exactly four in a row with both ends free.
bool isStraightFour(const Cells& c, int lo, int hi) {
	return hi - lo + 1 == 4 && c[lo - 1] == CellCode::Empty && c[hi + 1] == CellCode::Empty;
}

// Number of empty cells that would complete five through the centre: the
// gaps of every 5 cell segment holding the centre, 4 own stones and 1 empty.
int fiveCompletions(const Cells& c) {
	uint32_t gaps = 0;
	for (int start = 0; start <= kWindowRadius; ++start) {
		int own = 0;
		int gap = -1;
		for (int k = start; k < start + kAlignToWin; ++k) {
			if (c[k] == CellCode::Own) {
				++own;
			} else if (c[k] == CellCode::Empty) {
				gap = k;
			}
		}
		if (own == kAlignToWin - 1 && gap >= 0) {
			gaps |= 1U << gap;
		}
	}
	return std::popcount(gaps);
}

// Counts the empty cells whose filling turns the pattern into a shape in [lo, hi].
int movesReaching(const PatternTable& t, int index, Shape lo, Shape hi) {
	const Cells c = decode(index);
	int count = 0;
	for (int k = 0; k < kWindowCells; ++k) {
		if (c[k] != CellCode::Empty) {
			continue;
		}
		const int s = t[withOwn(index, k)] & kShapeMask;
		if (s >= lo && s <= hi) {
			++count;
		}
	}
	return count;
}

// Pairs of foe stones the centre stone can capture in one move, 0 to 2.
int captureThreats(const Cells& c) {
	const int m = kWindowRadius;
	const bool left =
		c[m - 1] == CellCode::Foe && c[m - 2] == CellCode::Foe && c[m - 3] == CellCode::Empty;
	const bool right =
		c[m + 1] == CellCode::Foe && c[m + 2] == CellCode::Foe && c[m + 3] == CellCode::Empty;
	return (left ? 1 : 0) + (right ? 1 : 0);
}

// Pairs of foe stones a stone on the cnetre captures right now, 0 to 2.
int capturesNow(const Cells& c) {
	const int m = kWindowRadius;
	const bool left =
		c[m - 1] == CellCode::Foe && c[m - 2] == CellCode::Foe && c[m - 3] == CellCode::Own;
	const bool right =
		c[m + 1] == CellCode::Foe && c[m + 2] == CellCode::Foe && c[m + 3] == CellCode::Own;
	return (left ? 1 : 0) + (right ? 1 : 0);
}

// Level 1: fives and fours, read straight from the cells.
void addFours(PatternTable& t) {
	for (int i = 0; i < kPatternCount; ++i) {
		const Cells c = decode(i);
		int lo = 0;
		int hi = 0;
		const int run = runThroughCentre(c, lo, hi);
		Shape s = kNone;
		if (run >= kAlignToWin) {
			s = kFive;
		} else if (isStraightFour(c, lo, hi)) {
			s = kOpenFour;
		} else {
			const int wins = fiveCompletions(c);
			s = wins >= 2 ? kOpenFour : wins == 1 ? kFour : kNone;
		}
		t[i] = static_cast<uint8_t>(s | (captureThreats(c) << kCaptureShift) |
									(capturesNow(c) << kTakeShift));
	}
}

// Level 2: threes, one move away from an open four (or from a four).
void addThrees(PatternTable& t) {
	for (int i = 0; i < kPatternCount; ++i) {
		if ((t[i] & kShapeMask) != kNone) {
			continue;
		}
		const int open = movesReaching(t, i, kOpenFour, kOpenFour);
		Shape s = kNone;
		if (open >= 2) {
			s = kOpenThree;
		} else if (open == 1) {
			s = kBrokenThree;
		} else if (movesReaching(t, i, kFour, kFour) > 0) {
			s = kSimpleThree;
		}
		t[i] = static_cast<uint8_t>(t[i] | s);
	}
}

// Level 3: twos, one move away from an open or broken three.
void addTwos(PatternTable& t) {
	for (int i = 0; i < kPatternCount; ++i) {
		if ((t[i] & kShapeMask) == kNone && movesReaching(t, i, kBrokenThree, kOpenThree) > 0) {
			t[i] = static_cast<uint8_t>(t[i] | kTwo);
		}
	}
}

// The pattern table, built once on first use. Each level reads only the
// levels below it, so the three passes must run in this order.
const PatternTable& patterns() {
	static const PatternTable table = [] {
		PatternTable t{};
		addFours(t);
		addThrees(t);
		addTwos(t);
		return t;
	}();
	return table;
}

struct Tally {
	std::array<int, kShapeCount> shapes{};
	int captureThreats = 0;
};

EvalWeights gWeights{};

// Value of one shape, spread over the stones that each count it once.
Score shapeValue(Shape s, const EvalWeights& w) {
	switch (s) {
		case kFive:
			return w.five / 5;
		case kOpenFour:
			return w.openFour / 4;
		case kFour:
			return w.simpleFour / 4;
		case kOpenThree:
			return w.openThree / 3;
		case kBrokenThree:
			return w.brokenThree / 3;
		case kSimpleThree:
			return w.simpleThree / 3;
		case kTwo:
			return w.openTwo / 2;
		default:
			return 0;
	}
}

// Value of `n` captured pairs, steeper as the capture win gets closer.
Score pairsValue(int n, const EvalWeights& w) {
	static constexpr std::array<int, kPairsToWin + 1> kScale = {0, 1, 3, 6, 12, 24};
	return kScale[n < 0 ? 0 : n > kPairsToWin ? kPairsToWin : n] * w.capturedPair;
}

// Looks up every stone on every axis and tallies shapes per colour.
void tally(const Board& b, std::array<Tally, 2>& out) {
	const PatternTable& table = patterns();
	for (Idx i = 0; i < kCellCount; ++i) {
		const Player p = b.at(i);
		if (p == Player::None) {
			continue;
		}
		Tally& t = out[playerIndex(p)];
		for (int axis = 0; axis < 4; ++axis) {
			const uint8_t entry = table[indexOf(b.window(i, axis, p))];
			++t.shapes[entry & kShapeMask];
			t.captureThreats += (entry >> kCaptureShift) & kCountMask;
		}
	}
}

// Material score of one side: its shapes, captures and capture threats.
Score sideScore(const Tally& t, int pairs, const EvalWeights& w) {
	Score total = pairsValue(pairs, w) + t.captureThreats * w.captureThreat;
	for (int s = kTwo; s < kShapeCount; ++s) {
		total += t.shapes[s] * shapeValue(static_cast<Shape>(s), w);
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

// Static evaluation from the point of view of `toMove`. Fours are settled by
// tempo first, everything else is the material difference, kept inside the
// decisive band so no weight setting can pass for a won game.
Score evaluate(const Board& b, Player toMove, const std::array<int, 2>& pairs) {
	const EvalWeights& w = gWeights;
	std::array<Tally, 2> tallies{};
	tally(b, tallies);
	const int me = playerIndex(toMove);
	const int foe = 1 - me;
	const Tally& mine = tallies[me];
	const Tally& theirs = tallies[foe];

	if (mine.shapes[kFour] + mine.shapes[kOpenFour] > 0) {
		return kDecisive;
	}
	if (pairs[me] == kPairsToWin - 1 && mine.captureThreats > 0) {
		return kDecisive;
	}
	if (theirs.shapes[kOpenFour] > 0) {
		return -kDecisive;
	}
	const Score diff = sideScore(mine, pairs[me], w) - sideScore(theirs, pairs[foe], w);
	return std::clamp(diff, -kDecisive + 1, kDecisive - 1);
}

// Static score of a candidate move, for ordering only: the shapes `p` makes
// by playing on `i` (attack) plus the ones it denies the foe there (defence).
Score moveScore(const Board& b, Idx i, Player p) {
	static constexpr std::array<Score, kShapeCount> kAttack = {
		0, 20, 60, 300, 400, 1500, 20000, 1000000,
	};
	static constexpr std::array<Score, kShapeCount> kDefend = {
		0, 10, 30, 200, 250, 1000, 10000, 500000,
	};
	const PatternTable& table = patterns();
	Score score = 0;
	for (int axis = 0; axis < 4; ++axis) {
		const int index = indexOf(b.window(i, axis, p));
		const uint8_t mine = table[index];
		const uint8_t theirs = table[mirrored(index)];
		score += kAttack[mine & kShapeMask] + kDefend[theirs & kShapeMask];
		score += ((mine >> kTakeShift) & kCountMask) * 2000;
		score += ((theirs >> kTakeShift) & kCountMask) * 1500;
	}
	return score;
}

}  // namespace gomoku::eval
