#include "gomoku/types.hpp"

namespace gomoku {

Player opponent(Player p) {
	return p == Player::Black ? Player::White : Player::Black;
}

int playerIndex(Player p) {
	return static_cast<int>(p) - 1;
}

int xOf(Idx i) {
	return i % kBoardSize;
}

int yOf(Idx i) {
	return i / kBoardSize;
}

Idx idxOf(int x, int y) {
	return static_cast<Idx>(y * kBoardSize + x);
}

bool inBounds(int x, int y) {
	return x >= 0 && x < kBoardSize && y >= 0 && y < kBoardSize;
}

const char* legalityText(Legality l) {
	switch (l) {
		case Legality::Legal:
			return "legal";
		case Legality::OutOfBounds:
			return "outside the board";
		case Legality::Occupied:
			return "intersection already taken";
		case Legality::DoubleThree:
			return "double-three is forbidden";
		case Legality::GameOver:
			return "the game is over";
		case Legality::NotYourTurn:
			return "not your turn";
	}
	return "unknown";
}

const char* legalityCode(Legality l) {
	switch (l) {
		case Legality::Legal:
			return "legal";
		case Legality::OutOfBounds:
			return "out_of_bounds";
		case Legality::Occupied:
			return "occupied";
		case Legality::DoubleThree:
			return "double_three";
		case Legality::GameOver:
			return "game_over";
		case Legality::NotYourTurn:
			return "not_your_turn";
	}
	return "unknown";
}

const char* winReasonCode(WinReason r) {
	switch (r) {
		case WinReason::None:
			return "";
		case WinReason::Alignment:
			return "five_in_a_row";
		case WinReason::Captures:
			return "captures";
		case WinReason::BoardFull:
			return "board_full";
		case WinReason::Resignation:
			return "resignation";
	}
	return "";
}
}  // namespace gomoku
