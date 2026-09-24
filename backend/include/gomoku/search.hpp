/* -------------------------------------------------------------------------- */
/*                              Module 2 (Search)                             */
/* -------------------------------------------------------------------------- */

//  Subject requirements, not negotiable :
//    - reach depth 10 or more in the tree ;
//    - stay under 0.5 s ON AVERAGE per move, so the budget is 450 ms ;
//    - the algorithm must be Min-Max, here in its negamax + alpha-beta form.
//
//  Roadmap, most profitable first :
//    [x] negamax + alpha-beta + iterative deepening + time budget
//    [ ] ordering: principal variation first, then killer moves, then history
//    [ ] Zobrist transposition table, keyed with sideToMoveKey()
//    [ ] principal variation search on non PV moves
//    [ ] forced threat detection, which collapses the branching factor
//    [ ] harder forward pruning on the candidate list
//
//  Good luck.

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include "gomoku/game.hpp"
#include "gomoku/types.hpp"

namespace gomoku {

struct SearchLimits {
	int maxDepth = 10;
	std::chrono::milliseconds budget{450};
	int maxCandidates = 20;
};

// Snapshot published at every iteration of the iterative deepening. These are
// exactly the fields the interface shows in its debug panel, and they map one
// to one onto the Stats type of docs/PROTOCOL.md.
struct SearchStats {
	int depth = 0;
	Score score = 0;
	Idx best = kNoIdx;
	uint64_t nodes = 0;
	uint64_t leaves = 0;
	uint64_t cutoffs = 0;
	uint64_t ttHits = 0;
	int64_t elapsedMs = 0;
	std::vector<Idx> pv;
	std::vector<std::pair<Idx, Score>> rootScores;
};

using ProgressFn = std::function<void(const SearchStats&)>;

struct SearchResult {
	Idx best = kNoIdx;
	SearchStats stats;
};

class Engine {
  public:
	Engine();

	// `game` is mutated while searching but comes back untouched. Returns
	// kNoIdx only when no legal move exists.
	SearchResult search(Game& game, const SearchLimits& limits, const ProgressFn& onProgress = {});

	// Asks for an early stop. With no search thread it only takes effect
	// between two moves.
	void requestStop();

  private:
	Score negamax(Game& game, int depth, int ply, Score alpha, Score beta);
	bool outOfTime() const;

	SearchLimits limits_{};
	SearchStats stats_{};
	std::atomic<bool> stop_{false};
	std::chrono::steady_clock::time_point start_{};
};

}  // namespace gomoku
