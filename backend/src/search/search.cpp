/* -------------------------------------------------------------------------- */
/*                                Search engine                               */
/* -------------------------------------------------------------------------- */

//  REFERENCE IMPLEMENTATION - negamax, alpha-beta, iterative deepening and a
//  hard time budget. It plays sensibly but does NOT reach the depth 10 the
//  subject demands: that is module 2's job, see the roadmap in search.hpp.
//
//  Known limitation: the double three ban is enforced at the ROOT only, since
//  checking it in every node would cost more than it buys. The engine can
//  therefore never play an illegal move, but its deep evaluation is slightly
//  optimistic (ADR-008).

#include "gomoku/search.hpp"

#include <algorithm>
#include <cstdlib>
#include <new>

#include "gomoku/eval.hpp"
#include "gomoku/movegen.hpp"

namespace gomoku {

// `bound` says whether `score` is exact or only a lower or upper bound, as
// aplha-beta left it. 16 bytes, so the table holds 2^20 of them in 16 MiB.
struct TTEntry {
	uint64_t key = 0;
	Score score = 0;
	Idx best = kNoIdx;
	int8_t depth = -1;
	uint8_t bound = 0;
};
namespace {

inline constexpr size_t kTTSize = size_t{1} << 20;
inline constexpr Score kMateZone = kScoreWin - 1000;
inline constexpr Score kForcedMove = 500000;
inline constexpr int kForcedSpare = 2;
inline constexpr int kBeamStep = 3;
inline constexpr int kMinBeam = 5;

enum Bound : uint8_t { kExact = 1, kLower = 2, kUpper = 3 };

// Win scores carry the ply they were found at; the table stores them relative
// to the node instead, so they stay right when reached through another path.
Score toTT(Score s, int ply) {
	return s >= kMateZone ? s + ply : s <= -kMateZone ? s - ply : s;
}

// Inverse of toTT, back to a score relative to the root.
Score fromTT(Score s, int ply) {
	return s >= kMateZone ? s - ply : s <= -kMateZone ? s + ply : s;
}

// Value of a finished game seen from `mover`. The ply term makes the search
// prefer winning soon and losing late.
Score terminalValue(GameStatus status, Player mover, int ply) {
	if (status == GameStatus::Draw) {
		return 0;
	}
	const bool moverWon = (status == GameStatus::BlackWins && mover == Player::Black) ||
						  (status == GameStatus::WhiteWins && mover == Player::White);
	return moverWon ? kScoreWin - ply : -(kScoreWin - ply);
}

// Sorts `moves` by decreasing static score, `first` ahead of everything when
// it is present. Returns how many moves win or stop a five on the spot.
int orderMoves(const Board& b, Player p, MoveList& moves, Idx first) {
	std::array<std::pair<Score, Idx>, kCellCount> scored{};
	int forced = 0;
	for (int k = 0; k < moves.count; ++k) {
		const Idx move = moves.moves[k];
		const Score score = eval::moveScore(b, move, p);
		if (score >= kForcedMove) {
			++forced;
		}
		scored[k] = {score + (move == first ? kScoreInf : 0), move};
	}
	std::sort(scored.begin(), scored.begin() + moves.count,
			  [](const std::pair<Score, Idx>& a, const std::pair<Score, Idx>& c) {
				  return a.first > c.first;
			  });
	for (int k = 0; k < moves.count; ++k) {
		moves.moves[k] = scored[k].second;
	}
	return forced;
}

// Keeps the best candidates of a node at `ply`: `maxCandidates` at the root and
// for its replies, kBeamStep fewer at each ply below, never under kMinBeam.
// When a five is on the line, only the forced moves and a spare are kept.
// A limit of 0 or less keeps everything.
void trimMoves(MoveList& moves, int maxCandidates, int ply, int forced) {
	if (maxCandidates <= 0) {
		return;
	}
	int width = std::max(kMinBeam, maxCandidates - kBeamStep * std::max(0, ply - 1));
	width = std::min(width, maxCandidates);
	if (forced > 0) {
		width = std::min(width, forced + kForcedSpare);
	}
	moves.count = std::min(moves.count, width);
}

}  // namespace

// Allocates the TT. Without memory the engine still works,
// it only loses the table.
Engine::Engine() : tt_(new (std::nothrow) TTEntry[kTTSize]) {}

// Defined here, where TTEntry is complete, so unique_ptr can delete it.
Engine::~Engine() = default;

// Zobrist key of the stones, the side to move and the captured pairs.
uint64_t Engine::positionKey(const Game& game) const {
	const std::array<int, 2> pairs = game.pairs();
	uint64_t key = game.board().hash();
	if (game.toMove() == Player::Black) {
		key ^= sideToMoveKey();
	}
	return key ^ (static_cast<uint64_t>(pairs[0] * 6 + pairs[1]) * 0x9E3779B97F4A7C15ULL);
}

// Slot holding `key`, or nullptr when the table is missing or holds another position.
TTEntry* Engine::probe(uint64_t key) {
	if (!tt_) {
		return nullptr;
	}
	TTEntry& e = tt_[key & (kTTSize - 1)];
	return e.key == key && e.depth >= 0 ? &e : nullptr;
}

// Records a finished node, classifying `score` against the original window.
void Engine::store(uint64_t key, int depth, int ply, Score score, Score alpha0, Score beta,
				   Idx best) {
	if (!tt_ || timeUp_) {
		return;
	}
	TTEntry& e = tt_[key & (kTTSize - 1)];
	e.key = key;
	e.score = toTT(score, ply);
	e.best = best;
	e.depth = static_cast<int8_t>(depth);
	e.bound = score <= alpha0 ? kUpper : score >= beta ? kLower : kExact;
}

// Asks for an early stop from another thread.
void Engine::requestStop() {
	stop_.store(true, std::memory_order_relaxed);
}

// True once the budget is spent or a stop was request.
bool Engine::outOfTime() const {
	if (stop_.load(std::memory_order_relaxed)) {
		return true;
	}
	return std::chrono::steady_clock::now() - start_ >= limits_.budget;
}

// Negamax with alpha-beta, scoring from the point of view of the side to move.
Score Engine::negamax(Game& game, int depth, int ply, Score alpha, Score beta) {
	++stats_.nodes;
	if ((stats_.nodes & 0x3FF) == 0 && outOfTime()) {
		timeUp_ = true;
		return 0;
	}
	if (depth <= 0) {
		++stats_.leaves;
		return eval::evaluate(game.board(), game.toMove(), game.pairs());
	}

	const uint64_t key = positionKey(game);
	const Score alpha0 = alpha;
	Idx ttMove = kNoIdx;
	if (const TTEntry* e = probe(key)) {
		++stats_.ttHits;
		ttMove = e->best;
		if (e->depth >= depth) {
			const Score s = fromTT(e->score, ply);
			if (e->bound == kExact || (e->bound == kLower && s >= beta) ||
				(e->bound == kUpper && s <= alpha)) {
				return s;
			}
		}
	}

	MoveList moves;
	movegen::generate(game.board(), moves);
	if (moves.count == 0) {
		++stats_.leaves;
		return eval::evaluate(game.board(), game.toMove(), game.pairs());
	}
	const int forced = orderMoves(game.board(), game.toMove(), moves, ttMove);
	trimMoves(moves, limits_.maxCandidates, ply, forced);

	Score best = -kScoreInf;
	Idx bestMove = kNoIdx;
	for (int k = 0; k < moves.count; ++k) {
		PlayedMove undo;
		game.makeMove(moves.moves[k], undo);
		const GameStatus status = game.terminalAfter(undo);
		const Score value = status != GameStatus::Ongoing
								? terminalValue(status, undo.player, ply)
								: -negamax(game, depth - 1, ply + 1, -beta, -alpha);
		game.unmakeMove(undo);

		if (value > best) {
			best = value;
			bestMove = moves.moves[k];
		}
		if (best > alpha) {
			alpha = best;
		}
		if (alpha >= beta) {
			++stats_.cutoffs;
			break;
		}
		if (outOfTime()) {
			timeUp_ = true;
			break;
		}
	}
	store(key, depth, ply, best, alpha0, beta, bestMove);
	return best;
}

// Iterative deepening from the root, publishing a snapshot per completed depth.
SearchResult Engine::search(Game& game, const SearchLimits& limits, const ProgressFn& onProgress) {
	limits_ = limits;
	stop_.store(false, std::memory_order_relaxed);
	start_ = std::chrono::steady_clock::now();
	stats_ = SearchStats{};
	timeUp_ = false;

	const Player me = game.toMove();

	// Full legality is filtered here, double three included, so the engine can
	// never propose a forbidden move.
	MoveList raw;
	movegen::generate(game.board(), raw);
	MoveList root;
	for (int k = 0; k < raw.count; ++k) {
		if (game.check(raw.moves[k], me) == Legality::Legal) {
			root.moves[root.count++] = raw.moves[k];
		}
	}
	if (root.count == 0) {
		return SearchResult{};
	}

	const int forced = orderMoves(game.board(), me, root, kNoIdx);
	trimMoves(root, limits.maxCandidates, 0, forced);
	Idx best = root.moves[0];

	for (int depth = 1; depth <= limits.maxDepth; ++depth) {
		Score alpha = -kScoreInf;
		Idx iterationBest = kNoIdx;
		Score iterationScore = -kScoreInf;
		std::vector<std::pair<Idx, Score>> rootScores;
		rootScores.reserve(static_cast<size_t>(root.count));
		bool aborted = false;

		orderMoves(game.board(), me, root, best);

		for (int k = 0; k < root.count; ++k) {
			PlayedMove undo;
			game.makeMove(root.moves[k], undo);
			const GameStatus status = game.terminalAfter(undo);
			const Score value = status != GameStatus::Ongoing
									? terminalValue(status, undo.player, 0)
									: -negamax(game, depth - 1, 1, -kScoreInf, -alpha);
			game.unmakeMove(undo);

			if (k > 0 && outOfTime()) {
				aborted = true;
				break;
			}
			rootScores.emplace_back(root.moves[k], value);
			if (value > iterationScore) {
				iterationScore = value;
				iterationBest = root.moves[k];
			}
			if (value > alpha) {
				alpha = value;
			}
		}

		if (!aborted && iterationBest != kNoIdx) {
			best = iterationBest;
			stats_.depth = depth;
			stats_.score = iterationScore;
			stats_.best = best;
			stats_.rootScores = std::move(rootScores);
			stats_.pv.assign(1, best);
			stats_.elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
								   std::chrono::steady_clock::now() - start_)
								   .count();
			if (onProgress) {
				onProgress(stats_);
			}
		}

		if (aborted || outOfTime()) {
			break;
		}
		if (std::abs(iterationScore) >= kScoreWin - 1000) {
			break;
		}
	}
	stats_.best = best;
	stats_.elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
						   std::chrono::steady_clock::now() - start_)
						   .count();
	return SearchResult{best, stats_};
}

}  // namespace gomoku
