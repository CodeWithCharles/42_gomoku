/* -------------------------------------------------------------------------- */
/*                                 Types tests                                */
/* -------------------------------------------------------------------------- */

#include <cstring>

#include "gomoku/types.hpp"
#include "harness.hpp"

namespace {

using namespace gomoku;

TEST(geometry_roundtrip) {
	CHECK(idxOf(0, 0) == 0);
	CHECK(idxOf(18, 18) == kCellCount - 1);
	CHECK(xOf(idxOf(7, 12)) == 7);
	CHECK(yOf(idxOf(7, 12)) == 12);
}

TEST(bounds_are_checked) {
	CHECK(inBounds(0, 0));
	CHECK(inBounds(18, 18));
	CHECK(!inBounds(-1, 0));
	CHECK(!inBounds(0, 19));
}

TEST(player_helpers) {
	CHECK(opponent(Player::Black) == Player::White);
	CHECK(opponent(Player::White) == Player::Black);
	CHECK(playerIndex(Player::Black) == 0);
	CHECK(playerIndex(Player::White) == 1);
}

TEST(legality_codes_are_stable) {
	CHECK(std::strcmp(legalityCode(Legality::DoubleThree), "double_three") == 0);
	CHECK(std::strcmp(legalityCode(Legality::Occupied), "occupied") == 0);
	CHECK(std::strcmp(legalityCode(Legality::NotYourTurn), "not_your_turn") == 0);
}

TEST(win_reason_codes_are_stable) {
	CHECK(std::strcmp(winReasonCode(WinReason::Alignment), "five_in_a_row") == 0);
	CHECK(std::strcmp(winReasonCode(WinReason::Captures), "captures") == 0);
	CHECK(std::strcmp(winReasonCode(WinReason::BoardFull), "board_full") == 0);
	CHECK(std::strcmp(winReasonCode(WinReason::None), "") == 0);
}

}  // namespace
