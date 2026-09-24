/* -------------------------------------------------------------------------- */
/*                                 Rules tests                                */
/* -------------------------------------------------------------------------- */

#include "gomoku/rules.hpp"
#include "harness.hpp"

namespace {

using namespace gomoku;

// Drops a stone with no arbitration, to build a test position.
void put(Board& b, int x, int y, Player p) {
	b.place(idxOf(x, y), p);
}

// Number of pairs `p` captures by playing on (x, y), stone placed beforehand.
int capturesAt(Board& b, int x, int y, Player p) {
	put(b, x, y, p);
	PlayedMove move;
	return rules::findCaptures(b, idxOf(x, y), p, move);
}

TEST(capture_works_in_every_direction) {
	const std::array<Delta, 8> dirs = {{
		{1, 0},
		{-1, 0},
		{0, 1},
		{0, -1},
		{1, 1},
		{-1, -1},
		{1, -1},
		{-1, 1},
	}};
	for (const Delta& d : dirs) {
		Board b;
		put(b, 9 + d.dx, 9 + d.dy, Player::White);
		put(b, 9 + 2 * d.dx, 9 + 2 * d.dy, Player::White);
		put(b, 9 + 3 * d.dx, 9 + 3 * d.dy, Player::Black);
		CHECK(capturesAt(b, 9, 9, Player::Black) == 1);
	}
}

TEST(capture_needs_exactly_two_stones) {
	Board three;
	put(three, 6, 5, Player::White);
	put(three, 7, 5, Player::White);
	put(three, 8, 5, Player::White);
	put(three, 9, 5, Player::Black);
	CHECK(capturesAt(three, 5, 5, Player::Black) == 0);

	Board single;
	put(single, 6, 5, Player::White);
	put(single, 7, 5, Player::Black);
	CHECK(capturesAt(single, 5, 5, Player::Black) == 0);
}

TEST(capture_never_takes_own_stones) {
	Board b;
	put(b, 6, 5, Player::Black);
	put(b, 7, 5, Player::Black);
	put(b, 8, 5, Player::Black);
	CHECK(capturesAt(b, 5, 5, Player::Black) == 0);
}

TEST(moving_between_two_enemies_is_safe) {
	Board b;
	put(b, 5, 5, Player::Black);
	put(b, 6, 5, Player::White);
	put(b, 8, 5, Player::Black);
	CHECK(capturesAt(b, 7, 5, Player::White) == 0);
	CHECK(!rules::canCaptureAnyPair(b, Player::White, Player::Black));
}

TEST(pair_becomes_vulnerable_again_after_a_flanker_returns) {
	Board b;
	put(b, 5, 5, Player::Black);
	put(b, 6, 5, Player::White);
	put(b, 7, 5, Player::White);
	put(b, 8, 5, Player::Black);

	b.remove(idxOf(5, 5));
	CHECK(rules::canCaptureAnyPair(b, Player::White, Player::Black));
	CHECK(capturesAt(b, 5, 5, Player::Black) == 1);
}

TEST(capture_works_at_the_board_edge) {
	Board b;
	put(b, 1, 0, Player::White);
	put(b, 2, 0, Player::White);
	put(b, 3, 0, Player::Black);
	PlayedMove move;
	put(b, 0, 0, Player::Black);
	CHECK(rules::findCaptures(b, idxOf(0, 0), Player::Black, move) == 1);
	CHECK(move.capturedCount == 2);
	CHECK(move.captured[0] == idxOf(1, 0));
	CHECK(move.captured[1] == idxOf(2, 0));
}

TEST(one_move_can_capture_several_pairs) {
	Board b;
	put(b, 10, 9, Player::White);
	put(b, 11, 9, Player::White);
	put(b, 12, 9, Player::Black);
	put(b, 9, 10, Player::White);
	put(b, 9, 11, Player::White);
	put(b, 9, 12, Player::Black);

	put(b, 9, 9, Player::Black);
	PlayedMove move;
	CHECK(rules::findCaptures(b, idxOf(9, 9), Player::Black, move) == 2);
	CHECK(move.capturedCount == 4);
}

TEST(alignment_of_five_or_more_is_detected) {
	Board five;
	for (int x = 5; x < 10; ++x) {
		put(five, x, 9, Player::Black);
	}
	CHECK(rules::hasAlignment(five, idxOf(7, 9), Player::Black));
	CHECK(rules::findAnyAlignment(five, Player::Black) != kNoIdx);
	CHECK(rules::findAnyAlignment(five, Player::White) == kNoIdx);

	Board six;
	for (int x = 5; x < 11; ++x) {
		put(six, x, 9, Player::Black);
	}
	CHECK(rules::hasAlignment(six, idxOf(7, 9), Player::Black));
}

TEST(four_in_a_row_is_not_an_alignment) {
	Board b;
	for (int x = 5; x < 9; ++x) {
		put(b, x, 9, Player::Black);
	}
	CHECK(!rules::hasAlignment(b, idxOf(6, 9), Player::Black));
	CHECK(rules::findAnyAlignment(b, Player::Black) == kNoIdx);
}

TEST(cross_of_four_forbids_the_centre) {
	Board b;
	put(b, 8, 9, Player::Black);
	put(b, 10, 9, Player::Black);
	put(b, 9, 8, Player::Black);
	put(b, 9, 10, Player::Black);
	CHECK(rules::createsDoubleThree(b, idxOf(9, 9), Player::Black));
}

TEST(a_single_three_is_allowed) {
	Board b;
	put(b, 8, 9, Player::Black);
	put(b, 10, 9, Player::Black);
	CHECK(!rules::createsDoubleThree(b, idxOf(9, 9), Player::Black));
}

TEST(a_broken_three_counts_as_a_free_three) {
	Board b;
	put(b, 6, 9, Player::Black);
	put(b, 7, 9, Player::Black);
	put(b, 9, 7, Player::Black);
	put(b, 9, 8, Player::Black);
	CHECK(rules::createsDoubleThree(b, idxOf(9, 9), Player::Black));
}

TEST(a_three_blocked_on_one_side_is_allowed) {
	Board b;
	put(b, 8, 9, Player::Black);
	put(b, 10, 9, Player::Black);
	put(b, 7, 9, Player::White);
	put(b, 11, 9, Player::White);
	put(b, 9, 8, Player::Black);
	put(b, 9, 10, Player::Black);
	CHECK(!rules::createsDoubleThree(b, idxOf(9, 9), Player::Black));
}

TEST(a_three_blocked_on_a_single_side_is_not_free) {
	Board before;
	put(before, 8, 9, Player::Black);
	put(before, 10, 9, Player::Black);
	put(before, 7, 9, Player::White);
	put(before, 9, 8, Player::Black);
	put(before, 9, 10, Player::Black);
	CHECK(!rules::createsDoubleThree(before, idxOf(9, 9), Player::Black));

	Board after;
	put(after, 8, 9, Player::Black);
	put(after, 10, 9, Player::Black);
	put(after, 11, 9, Player::White);
	put(after, 9, 8, Player::Black);
	put(after, 9, 10, Player::Black);
	CHECK(!rules::createsDoubleThree(after, idxOf(9, 9), Player::Black));
}

TEST(double_three_on_the_two_diagonals) {
	Board b;
	put(b, 8, 8, Player::Black);
	put(b, 10, 10, Player::Black);
	put(b, 8, 10, Player::Black);
	put(b, 10, 8, Player::Black);
	CHECK(rules::createsDoubleThree(b, idxOf(9, 9), Player::Black));
}

TEST(double_three_check_leaves_the_board_untouched) {
	Board b;
	put(b, 8, 9, Player::Black);
	put(b, 10, 9, Player::Black);
	put(b, 9, 8, Player::Black);
	put(b, 9, 10, Player::Black);
	const uint64_t before = b.hash();
	const int stones = b.stoneCount();

	rules::createsDoubleThree(b, idxOf(9, 9), Player::Black);

	CHECK(b.hash() == before);
	CHECK(b.stoneCount() == stones);
	CHECK(b.isEmpty(idxOf(9, 9)));
}

TEST(a_plain_five_cannot_be_broken_along_its_own_axis) {
	Board b;
	for (int x = 5; x < 10; ++x) {
		put(b, x, 9, Player::Black);
	}
	CHECK(!rules::canBreakAlignment(b, Player::Black, idxOf(7, 9), Player::White));
}

TEST(a_five_with_a_capturable_pair_can_be_broken) {
	Board b;
	for (int x = 5; x < 10; ++x) {
		put(b, x, 9, Player::Black);
	}
	put(b, 7, 10, Player::Black);
	put(b, 7, 8, Player::White);
	CHECK(rules::canBreakAlignment(b, Player::Black, idxOf(7, 9), Player::White));
}

TEST(a_capturable_pair_outside_the_line_does_not_break_it) {
	Board b;
	for (int x = 5; x < 10; ++x) {
		put(b, x, 9, Player::Black);
	}
	put(b, 2, 2, Player::Black);
	put(b, 2, 3, Player::Black);
	put(b, 2, 1, Player::White);

	CHECK(rules::canCaptureAnyPair(b, Player::Black, Player::White));
	CHECK(!rules::canBreakAlignment(b, Player::Black, idxOf(7, 9), Player::White));
}

TEST(can_capture_any_pair_needs_a_free_flank) {
	Board closed;
	put(closed, 5, 5, Player::Black);
	put(closed, 6, 5, Player::Black);
	put(closed, 4, 5, Player::White);
	put(closed, 7, 5, Player::White);
	CHECK(!rules::canCaptureAnyPair(closed, Player::Black, Player::White));

	Board open;
	put(open, 5, 5, Player::Black);
	put(open, 6, 5, Player::Black);
	put(open, 4, 5, Player::White);
	CHECK(rules::canCaptureAnyPair(open, Player::Black, Player::White));
}

TEST(can_break_alignment_handles_a_missing_anchor) {
	Board b;
	CHECK(!rules::canBreakAlignment(b, Player::Black, kNoIdx, Player::White));
}

}  // namespace
