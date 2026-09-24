// ============================================================================
//  Bench - module 2's working instrument.
//
//  The subject pins two numbers: depth 10 or more, under 0.5 s on average.
//  This binary measures them on fixed positions, so every optimisation is
//  backed by a before and after instead of an impression.
//
//    make bench              runs every position with the default budget
//    ./gomoku_bench 1000     runs them with a 1000 ms budget
// ============================================================================

#include <chrono>
#include <cstdio>
#include <cstdlib>
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

}  // namespace

// Runs every benchmark position under the given budget.
int main(int argc, char** argv) {
	const int budgetMs = argc > 1 ? std::atoi(argv[1]) : 450;
	std::printf("\nBudget: %d ms per move   (subject wants depth >= 10 under 500 ms)\n\n",
				budgetMs);
	for (const Position& position : kPositions) {
		runOne(position, budgetMs);
	}
	std::printf("\n");
	return 0;
}
