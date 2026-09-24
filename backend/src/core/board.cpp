/* -------------------------------------------------------------------------- */
/*                                    Board                                   */
/* -------------------------------------------------------------------------- */

#include "gomoku/board.hpp"

namespace gomoku {
namespace {

struct ZobristTable {
	std::array<std::array<uint64_t, kCellCount>, 2> piece;
	uint64_t sideToMove;
};

// splitmix64, enough for Zobrist keys and usable in a constant expression.
constexpr uint64_t nextRandom(uint64_t& state) {
	state += 0x9E3779B97F4A7C15ULL;
	uint64_t z = state;
	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
	z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
	return z ^ (z >> 31);
}

// Builds the whole Zobrist table from a fixed seed.
constexpr ZobristTable buildZobrist() {
	ZobristTable table{};
	uint64_t state = 0x9E3779B97F4A7C15ULL;
	for (auto& side : table.piece) {
		for (auto& cell : side) {
			cell = nextRandom(state);
		}
	}
	table.sideToMove = nextRandom(state);
	return table;
}

// constinit : built at compile time, so no initialisation order to get wrong
// and no init() anyone could forget to call.
constinit const ZobristTable kZobrist = buildZobrist();

}  // namespace

// Zobrist key to mix in when the side to move is Black.
uint64_t sideToMoveKey() {
	return kZobrist.sideToMove;
}

Board::Board() {
	clear();
}

// Empties the board and resets counters and hash.
void Board::clear() {
	cells_.fill(Player::None);
	perPlayer_ = {0, 0};
	stoneCount_ = 0;
	hash_ = 0;
}

Player Board::at(Idx i) const {
	return cells_[i];
}

Player Board::at(int x, int y) const {
	return cells_[idxOf(x, y)];
}

bool Board::isEmpty(Idx i) const {
	return cells_[i] == Player::None;
}

void Board::place(Idx i, Player p) {
	const int side = playerIndex(p);
	cells_[i] = p;
	++perPlayer_[side];
	++stoneCount_;
	hash_ ^= kZobrist.piece[side][i];
}

void Board::remove(Idx i) {
	const Player p = cells_[i];
	if (p == Player::None) {
		return;
	}
	const int side = playerIndex(p);
	cells_[i] = Player::None;
	--perPlayer_[side];
	--stoneCount_;
	hash_ ^= kZobrist.piece[side][i];
}

int Board::stoneCount() const {
	return stoneCount_;
}

int Board::stoneCount(Player p) const {
	return perPlayer_[playerIndex(p)];
}

bool Board::isFull() const {
	return stoneCount_ == kCellCount;
}

uint64_t Board::hash() const {
	return hash_;
}

int Board::countLine(Idx i, int axis, Player p) const {
	const Delta d = kAxes[axis];
	int count = 1;
	for (int sign = -1; sign <= 1; sign += 2) {
		int x = xOf(i) + sign * d.dx;
		int y = yOf(i) + sign * d.dy;
		while (inBounds(x, y) && cells_[idxOf(x, y)] == p) {
			++count;
			x += sign * d.dx;
			y += sign * d.dy;
		}
	}
	return count;
}

LineWindow Board::window(Idx i, int axis, Player viewer) const {
	const Delta d = kAxes[axis];
	const int cx = xOf(i);
	const int cy = yOf(i);
	LineWindow packed = 0;
	for (int k = -kWindowRadius; k <= kWindowRadius; ++k) {
		const int x = cx + k * d.dx;
		const int y = cy + k * d.dy;
		CellCode code = CellCode::Wall;
		if (inBounds(x, y)) {
			const Player cell = cells_[idxOf(x, y)];
			if (cell == Player::None) {
				code = CellCode::Empty;
			} else {
				code = cell == viewer ? CellCode::Own : CellCode::Foe;
			}
		}
		packed |= static_cast<LineWindow>(code) << (2 * (k + kWindowRadius));
	}
	return packed;
}

bool Board::hasNeighbor(Idx i, int radius) const {
	const int cx = xOf(i);
	const int cy = yOf(i);
	for (int dy = -radius; dy <= radius; ++dy) {
		for (int dx = -radius; dx <= radius; ++dx) {
			if (dx == 0 && dy == 0) {
				continue;
			}
			const int x = cx + dx;
			const int y = cy + dy;
			if (inBounds(x, y) && cells_[idxOf(x, y)] != Player::None) {
				return true;
			}
		}
	}
	return false;
}

}  // namespace gomoku
