// ============================================================================
//  Bench - module 2's working instrument.
//
//  The subject pins two numbers: depth 10 or more, under 0.5 s on average.
//  This binary measures them on fixed positions, so every optimisation is
//  backed by a before and after instead of an impression. The positions are
//  the hardest moments of a game; the games mode measures what the subject
//  actually asks, an average over whole games played the way Session plays.
//
//    make bench                    runs every position with the default budget
//    ./gomoku_bench 1000           runs them with a 1000 ms budget
//    make bench-games              plays 8 engine versus engine games
//    ./gomoku_bench games 4 450    plays 4 games, 450 ms on average
// ============================================================================

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

#include "gomoku/game.hpp"
#include "gomoku/search.hpp"

using namespace gomoku;

namespace {

struct Position {
	const char* name;
	std::vector<std::pair<int, int>> moves;
};

const std::vector<Position> kPositions = {
	{"opening", {{9, 9}}},
	{"quiet midgame", {{9, 9}, {10, 10}, {9, 10}, {10, 9}, {8, 9}, {11, 10}}},
	{"direct threat", {{9, 9}, {3, 3}, {9, 10}, {3, 4}, {9, 11}, {3, 5}, {8, 8}, {4, 4}}},
	{"captures in play", {{9, 9}, {10, 9}, {11, 9}, {8, 9}, {9, 10}, {10, 10}, {11, 11}, {8, 8}}},
};

// Plays the setup moves, then searches once and prints what it reached.
void runOne(const Position& position, int budgetMs) {
	Game game;
	for (const auto& move : position.moves) {
		if (game.play(idxOf(move.first, move.second)) != Legality::Legal) {
			std::printf("  %-18s SETUP REFUSED at (%d,%d)\n", position.name, move.first,
						move.second);
			return;
		}
	}

	Engine engine;
	SearchLimits limits;
	limits.maxDepth = 20;
	limits.budget = std::chrono::milliseconds(budgetMs);

	const auto start = std::chrono::steady_clock::now();
	const SearchResult result = engine.search(game, limits);
	const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
							 std::chrono::steady_clock::now() - start)
							 .count();

	const double nps = elapsed > 0 ? static_cast<double>(result.stats.nodes) * 1000.0 /
										 static_cast<double>(elapsed)
								   : 0.0;
	std::printf("  %-18s depth %2d  %9llu nodes  %5lld ms  %9.0f n/s  %s\n", position.name,
				result.stats.depth, static_cast<unsigned long long>(result.stats.nodes),
				static_cast<long long>(elapsed), nps,
				result.stats.depth >= 10 ? "OK" : "DEPTH TOO LOW");
}

// Second stone of each game, the first being the centre. Distinct openings
// keep the games from repeating each other.
const std::vector<std::pair<int, int>> kOpenings = {
	{10, 10}, {9, 10}, {11, 10}, {10, 8}, {8, 11}, {11, 9}, {8, 8}, {7, 9},
};

inline constexpr int kGameMaxDepth = 10;
inline constexpr int kDepthBuckets = 21;

struct GameTally {
	std::array<int, kDepthBuckets> depths{};
	int moves = 0;
	int proven = 0;
	int quiet = 0;
	int quietDeep = 0;
	int64_t totalMs = 0;
	int64_t slowestMs = 0;
	double worstAverage = 0.0;
};

// Plays the engine against itself from the centre and `second`, the way
// Session does: depth capped at 10 and the average time policy. Every search
// is booked into `tally`.
void playGame(int number, std::pair<int, int> second, int budgetMs, GameTally& tally) {
	Game game;
	game.play(idxOf(kBoardSize / 2, kBoardSize / 2));
	game.play(idxOf(second.first, second.second));

	Engine engine;
	TimeBank bank;
	const std::chrono::milliseconds target(budgetMs);
	int64_t gameMs = 0;
	int searches = 0;
	while (game.status() == GameStatus::Ongoing) {
		SearchLimits limits;
		limits.maxDepth = kGameMaxDepth;
		limits.budget = bank.next(target);
		const SearchResult result = engine.search(game, limits);
		if (result.best == kNoIdx || game.play(result.best) != Legality::Legal) {
			break;
		}
		const int64_t ms = result.stats.elapsedMs;
		bank.record(ms);
		gameMs += ms;
		++searches;

		++tally.moves;
		tally.totalMs += ms;
		tally.slowestMs = std::max(tally.slowestMs, ms);
		tally.worstAverage = std::max(tally.worstAverage, static_cast<double>(gameMs) / searches);
		++tally.depths[std::min(result.stats.depth, kDepthBuckets - 1)];
		if (std::abs(result.stats.score) >= kScoreWin - 1000) {
			++tally.proven;
		} else {
			++tally.quiet;
			tally.quietDeep += result.stats.depth >= kGameMaxDepth ? 1 : 0;
		}
	}

	const char* outcome = game.status() == GameStatus::BlackWins   ? "black wins"
						  : game.status() == GameStatus::WhiteWins ? "white wins"
						  : game.status() == GameStatus::Draw	   ? "draw"
																   : "unfinished";
	std::printf("  game %2d  (9,9) (%2d,%2d)  %-10s %-14s %3d moves  avg %4.0f ms\n", number,
				second.first, second.second, outcome, winReasonCode(game.winReason()),
				static_cast<int>(game.history().size()),
				searches > 0 ? static_cast<double>(gameMs) / searches : 0.0);
}

// Plays `count` games and prints what the subject measures: average time per
// move, depth reached, and how many moves were settled by a proven result.
int runGames(int count, int budgetMs) {
	std::printf("\nGames: %d, engine versus engine, depth capped at %d, %d ms on average\n\n",
				count, kGameMaxDepth, budgetMs);
	GameTally tally;
	for (int k = 0; k < count; ++k) {
		playGame(k + 1, kOpenings[static_cast<size_t>(k) % kOpenings.size()], budgetMs, tally);
	}
	if (tally.moves == 0) {
		std::printf("\n  no move played\n\n");
		return 1;
	}

	const double average = static_cast<double>(tally.totalMs) / tally.moves;
	std::printf("\n  engine moves          %d\n", tally.moves);
	std::printf("  average time          %.0f ms   %s\n", average,
				average < 500.0 ? "OK" : "TOO SLOW");
	std::printf("  worst running average %.0f ms\n", tally.worstAverage);
	std::printf("  slowest move          %lld ms\n", static_cast<long long>(tally.slowestMs));
	std::printf("  proven win or loss    %d moves (%.0f %%), search stops once certain\n",
				tally.proven, 100.0 * tally.proven / tally.moves);
	std::printf("  depth >= %d           %.0f %% of the other moves\n", kGameMaxDepth,
				tally.quiet > 0 ? 100.0 * tally.quietDeep / tally.quiet : 0.0);
	std::printf("\n  depth   moves\n");
	for (int depth = 0; depth < kDepthBuckets; ++depth) {
		if (tally.depths[depth] > 0) {
			std::printf("  %5d   %5d\n", depth, tally.depths[depth]);
		}
	}
	std::printf("\n");
	return 0;
}

}  // namespace

// Runs every benchmark position under the given budget, or, with "games",
// plays engine versus engine games: gomoku_bench games [count] [budgetMs].
int main(int argc, char** argv) {
	if (argc > 1 && std::strcmp(argv[1], "games") == 0) {
		const int count = argc > 2 ? std::max(1, std::atoi(argv[2])) : 8;
		const int budgetMs = argc > 3 ? std::max(10, std::atoi(argv[3])) : 450;
		return runGames(count, budgetMs);
	}
	const int budgetMs = argc > 1 ? std::atoi(argv[1]) : 450;
	std::printf("\nBudget: %d ms per move   (subject wants depth >= 10 under 500 ms)\n\n",
				budgetMs);
	for (const Position& position : kPositions) {
		runOne(position, budgetMs);
	}
	std::printf("\n");
	return 0;
}
