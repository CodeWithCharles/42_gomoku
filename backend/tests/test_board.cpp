/* -------------------------------------------------------------------------- */
/*                                 Board tests                                */
/* -------------------------------------------------------------------------- */

#include "gomoku/board.hpp"
#include "harness.hpp"

namespace {

using namespace gomoku;

// Reads back the cell stored at offset `k` of a packed window.
CellCode cellAt(LineWindow window, int k) {
	const unsigned shift = static_cast<unsigned>(2 * (k + kWindowRadius));
	return static_cast<CellCode>((window >> shift) & 0x3u);
}

TEST(board_starts_empty) {
	Board board;
	CHECK(board.stoneCount() == 0);
	CHECK(board.stoneCount(Player::Black) == 0);
	CHECK(board.hash() == 0);
	CHECK(board.isEmpty(idxOf(9, 9)));
	CHECK(!board.isFull());
}

TEST(place_updates_counts_and_hash) {
	Board board;
	board.place(idxOf(9, 9), Player::Black);
	CHECK(board.at(9, 9) == Player::Black);
	CHECK(board.at(idxOf(9, 9)) == Player::Black);
	CHECK(board.stoneCount() == 1);
	CHECK(board.stoneCount(Player::Black) == 1);
	CHECK(board.stoneCount(Player::White) == 0);
	CHECK(board.hash() != 0);
}

TEST(remove_restores_hash) {
	Board board;
	board.place(idxOf(3, 4), Player::White);
	board.place(idxOf(5, 6), Player::Black);
	const uint64_t withTwo = board.hash();

	board.place(idxOf(7, 8), Player::White);
	CHECK(board.hash() != withTwo);
	board.remove(idxOf(7, 8));
	CHECK(board.hash() == withTwo);

	board.remove(idxOf(3, 4));
	board.remove(idxOf(5, 6));
	CHECK(board.hash() == 0);
	CHECK(board.stoneCount() == 0);
}

TEST(hash_is_order_independent) {
	Board first;
	first.place(idxOf(1, 1), Player::Black);
	first.place(idxOf(2, 2), Player::White);

	Board second;
	second.place(idxOf(2, 2), Player::White);
	second.place(idxOf(1, 1), Player::Black);

	CHECK(first.hash() == second.hash());
}

TEST(hash_separates_colors_and_squares) {
	Board black;
	black.place(idxOf(9, 9), Player::Black);
	Board white;
	white.place(idxOf(9, 9), Player::White);
	Board elsewhere;
	elsewhere.place(idxOf(9, 10), Player::Black);

	CHECK(black.hash() != white.hash());
	CHECK(black.hash() != elsewhere.hash());
	CHECK(sideToMoveKey() != 0);
}

TEST(remove_on_empty_cell_is_a_noop) {
	Board board;
	board.place(idxOf(9, 9), Player::Black);
	const uint64_t before = board.hash();
	board.remove(idxOf(4, 4));
	CHECK(board.hash() == before);
	CHECK(board.stoneCount() == 1);
}

TEST(clear_resets_everything) {
	Board board;
	board.place(idxOf(9, 9), Player::Black);
	board.place(idxOf(9, 10), Player::White);
	board.clear();
	CHECK(board.stoneCount() == 0);
	CHECK(board.hash() == 0);
	CHECK(board.at(idxOf(9, 9)) == Player::None);
}

TEST(count_line_spans_the_whole_run) {
	Board board;
	for (int x = 5; x < 10; ++x) {
		board.place(idxOf(x, 9), Player::Black);
	}
	CHECK(board.countLine(idxOf(7, 9), 0, Player::Black) == 5);
	CHECK(board.countLine(idxOf(5, 9), 0, Player::Black) == 5);
	CHECK(board.countLine(idxOf(9, 9), 0, Player::Black) == 5);
	CHECK(board.countLine(idxOf(7, 9), 1, Player::Black) == 1);
}

TEST(count_line_works_on_every_axis) {
	Board board;
	for (int k = -2; k <= 2; ++k) {
		board.place(idxOf(9 + k, 9), Player::Black);
		board.place(idxOf(4, 9 + k), Player::White);
		board.place(idxOf(14 + k, 4 + k), Player::Black);
		board.place(idxOf(4 + k, 14 - k), Player::White);
	}
	CHECK(board.countLine(idxOf(9, 9), 0, Player::Black) == 5);
	CHECK(board.countLine(idxOf(4, 9), 1, Player::White) == 5);
	CHECK(board.countLine(idxOf(14, 4), 2, Player::Black) == 5);
	CHECK(board.countLine(idxOf(4, 14), 3, Player::White) == 5);
}

TEST(count_line_stops_at_the_edge) {
	Board board;
	board.place(idxOf(0, 0), Player::Black);
	board.place(idxOf(1, 0), Player::Black);
	CHECK(board.countLine(idxOf(0, 0), 0, Player::Black) == 2);
	CHECK(board.countLine(idxOf(0, 0), 1, Player::Black) == 1);
}

TEST(count_line_ignores_the_centre_stone) {
	Board board;
	board.place(idxOf(8, 9), Player::Black);
	board.place(idxOf(10, 9), Player::Black);
	CHECK(board.countLine(idxOf(9, 9), 0, Player::Black) == 3);
	CHECK(board.isEmpty(idxOf(9, 9)));
}

TEST(window_encodes_own_foe_and_empty) {
	Board board;
	board.place(idxOf(8, 9), Player::Black);
	board.place(idxOf(10, 9), Player::White);

	const LineWindow seenByBlack = board.window(idxOf(9, 9), 0, Player::Black);
	CHECK(cellAt(seenByBlack, -1) == CellCode::Own);
	CHECK(cellAt(seenByBlack, 1) == CellCode::Foe);
	CHECK(cellAt(seenByBlack, 0) == CellCode::Empty);
	CHECK(cellAt(seenByBlack, 4) == CellCode::Empty);

	const LineWindow seenByWhite = board.window(idxOf(9, 9), 0, Player::White);
	CHECK(cellAt(seenByWhite, -1) == CellCode::Foe);
	CHECK(cellAt(seenByWhite, 1) == CellCode::Own);
}

TEST(window_marks_off_board_cells_as_wall) {
	Board board;
	const LineWindow window = board.window(idxOf(1, 9), 0, Player::Black);
	CHECK(cellAt(window, -4) == CellCode::Wall);
	CHECK(cellAt(window, -3) == CellCode::Wall);
	CHECK(cellAt(window, -2) == CellCode::Wall);
	CHECK(cellAt(window, -1) == CellCode::Empty);
	CHECK(cellAt(window, 4) == CellCode::Empty);
}

TEST(has_neighbor_respects_radius) {
	Board board;
	board.place(idxOf(9, 9), Player::Black);
	CHECK(board.hasNeighbor(idxOf(10, 10), 1));
	CHECK(!board.hasNeighbor(idxOf(11, 11), 1));
	CHECK(board.hasNeighbor(idxOf(11, 11), 2));
	CHECK(!board.hasNeighbor(idxOf(9, 9), 1));
}

}  // namespace
