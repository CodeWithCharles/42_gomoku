/* -------------------------------------------------------------------------- */
/*                                    Board */
/* -------------------------------------------------------------------------- */

#pragma once

#include <array>
#include <cstdint>

#include "gomoku/types.hpp"

namespace gomoku {

using LineWindow = uint32_t;
inline constexpr int kWindowRadius = 4;
inline constexpr int kWindowCells = 2 * kWindowRadius + 1;

enum class CellCode : uint32_t { Empty = 0, Own = 1, Foe = 2, Wall = 3 };

// Zobrist key to mix in when the side to move is Black. Board does not know
// whose turn it is, so the caller owns this part of the key
uint64_t sideToMoveKey();

class Board {
  public:
	Board();

	void clear();

	Player at(Idx i) const;
	Player at(int x, int y) const;
	bool isEmpty(Idx i) const;

	// Place and remove perform NO validation: legality is settled
	// by Game beforehand. Both update the Zobrist hash incrementally.
	void place(Idx i, Player p);
	void remove(Idx i);

	int stoneCount() const;
	int stoneCount(Player p) const;
	bool isFull() const;

	// Hash of the stones only. Combine with sideToMoveKey() to key a
	// transposition table.
	uint64_t hash() const;

	// Length of the run of `p` crossing `i` along `axis`, counting `i` itself.
	// Does not check that the stone on `i` belongs to `p`, so calling it on an
	// empty cell answers "how long would the run be if `p` played here".
	int countLine(Idx i, int axis, Player p) const;

	// Window centred on `i` along `axis`, seen from `viewer`. Off-board cells
	// read as CellCode::Wall.
	LineWindow window(Idx i, int axis, Player viewer) const;

	// True if any stone sits within Chebyshev distance `radius` of `i`.
	bool hasNeighbor(Idx i, int radius) const;

  private:
	std::array<Player, kCellCount> cells_{};
	std::array<int, 2> perPlayer_{};
	int stoneCount_ = 0;
	uint64_t hash_ = 0;
};

}  // namespace gomoku
