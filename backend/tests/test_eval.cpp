/* -------------------------------------------------------------------------- */
/*                              Evaluation tests                              */
/* -------------------------------------------------------------------------- */

#include "gomoku/eval.hpp"
#include "harness.hpp"

namespace {

using namespace gomoku;

constexpr std::array<int, 2> kNoPairs = {0, 0};

// Places `p` on every (x, 9) listed, row 9 being far from any edge.
void row(Board& b, Player p, std::initializer_list<int> xs) {
	for (const int x : xs) {
		b.place(idxOf(x, 9), p);
	}
}

TEST(eval_a_broken_three_beats_a_blocked_three) {
	Board broken;
	row(broken, Player::Black, {5, 7, 8});
	Board blocked;
	row(blocked, Player::Black, {5, 6, 7});
	row(blocked, Player::White, {4});
	row(broken, Player::White, {14});
	CHECK(eval::evaluate(broken, Player::Black, kNoPairs) >
		  eval::evaluate(blocked, Player::Black, kNoPairs));
}

TEST(eval_a_three_without_room_is_worthless) {
	Board dead;
	row(dead, Player::White, {3, 8});
	row(dead, Player::Black, {4, 5, 6});
	Board alone;
	row(alone, Player::White, {3, 8});
	row(alone, Player::Black, {4});
	CHECK(eval::evaluate(dead, Player::Black, kNoPairs) ==
		  eval::evaluate(alone, Player::Black, kNoPairs));
}

TEST(eval_a_four_to_move_outweighs_open_threes) {
	Board b;
	row(b, Player::White, {4});
	row(b, Player::Black, {5, 6, 7, 8});
	for (int y = 3; y < 6; ++y) {
		b.place(idxOf(14, y), Player::White);
		b.place(idxOf(y + 8, 15), Player::White);
	}
	CHECK(eval::evaluate(b, Player::Black, kNoPairs) > 0);
}

TEST(eval_values_a_capture_threat) {
	Board b;
	row(b, Player::Black, {5});
	row(b, Player::White, {6, 7});
	CHECK(eval::evaluate(b, Player::Black, kNoPairs) > 0);
}

TEST(eval_four_pairs_and_a_capture_threat_is_decisive) {
	Board b;
	row(b, Player::Black, {5});
	row(b, Player::White, {6, 7});
	const std::array<int, 2> pairs = {4, 0};
	CHECK(eval::evaluate(b, Player::Black, pairs) > eval::weights().five);
}

}  // namespace
