/* -------------------------------------------------------------------------- */
/*                             Types and constants                            */
/* -------------------------------------------------------------------------- */

#pragma once

#include <array>
#include <cstdint>

namespace gomoku {

inline constexpr int kBoardSize = 19;
inline constexpr int kCellCount = kBoardSize * kBoardSize;
inline constexpr int kAlignToWin = 5;
inline constexpr int kPairsToWin = 5;

/* --------------------- Linear index of an intersection -------------------- */
using Idx = int16_t;
inline constexpr Idx kNoIdx = -1;

/* ------------------ Evaluation score, negamax convention ------------------ */
using Score = int32_t;
inline constexpr Score kScoreWin = 1'000'000;
inline constexpr Score kScoreInf = 2'000'000;

enum class Player : uint8_t { None = 0, Black = 1, White = 2 };

/* -------------------------- Gives opponent color -------------------------- */
Player opponent(Player p);

/* ------------------------ Gives player index 0 or 1 ----------------------- */
int playerIndex(Player p);

/* ------------------------ Column of an intersection ----------------------- */
int xOf(Idx i);

/* ------------------------- Row of an intersection ------------------------- */
int yOf(Idx i);

/* ---------------------- Linear index from coordinates --------------------- */
Idx idxOf(int x, int y);

bool inBounds(int x, int y);

struct Delta {
	int dx;
	int dy;
};

/* -- The 4 axes for alignment : horizontal, vertical, diagonal up / down. -- */
inline constexpr std::array<Delta, 4> kAxes = {{
	{1, 0},
	{0, 1},
	{1, 1},
	{1, -1},
}};

enum class GameStatus : uint8_t { Ongoing, BlackWins, WhiteWins, Draw };

enum class WinReason : uint8_t { None, Alignment, Captures, BoardFull, Resignation };

enum class Legality : uint8_t {
	Legal,
	OutOfBounds,
	Occupied,
	DoubleThree,
	GameOver,
	NotYourTurn,
};

/* -------------------- Debug message for forbidden move -------------------- */
const char* legalityText(Legality l);

/* ------------ Stable code of a forbidden move for the protocol ------------ */
const char* legalityCode(Legality l);

/* ---------- Stable code of an end game condition for the protocol --------- */
const char* winReasonCode(WinReason r);

/* -- Played move / what's needed to cancel it. `captured` list the removed - */
/* ----- stones : at most 4 pairs if the move is intersecting the 4 axes ---- */
struct PlayedMove {
	Idx idx = kNoIdx;
	Player player = Player::None;
	uint8_t capturedCount = 0;
	std::array<Idx, 8> captured{};
};

}  // namespace gomoku
