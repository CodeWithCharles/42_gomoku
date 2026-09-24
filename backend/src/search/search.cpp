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

#include "gomoku/eval.hpp"
#include "gomoku/movegen.hpp"

namespace gomoku {
namespace {

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
// it is present. Scores are computed once, not inside the comparator.
void orderMoves(const Board& b, Player p, MoveList& moves, Idx first) {
	std::array<std::pair<Score, Idx>, kCellCount> scored{};
	for (int k = 0; k < moves.count; ++k) {
		const Idx move = moves.moves[k];
		const Score bonus = move == first ? kScoreInf : 0;
		scored[k] = {eval::moveScore(b, move, p) + bonus, move};
	}
	std::sort(scored.begin(), scored.begin() + moves.count,
			  [](const std::pair<Score, Idx>& a, const std::pair<Score, Idx>& c) {
				  return a.first > c.first;
			  });
	for (int k = 0; k < moves.count; ++k) {
		moves.moves[k] = scored[k].second;
	}
}

}  // namespace

Engine::Engine() = default;

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

// Negamax with alpha-beta, socring from the point of view of the side to move.
Score Engine::negamax(Game& game, int depth, int ply, Score alpha, Score beta) {
	++stats_.nodes;
	if ((stats_.nodes & 0x3FF) == 0 && outOfTime()) {
		return 0;
	}
	if (depth <= 0) {
		++stats_.leaves;
		return eval::evaluate(game.board(), game.toMove(), game.pairs());
	}

	MoveList moves;
	movegen::generate(game.board(), moves);
	if (moves.count == 0) {
		++stats_.leaves;
		return eval::evaluate(game.board(), game.toMove(), game.pairs());
	}
	orderMoves(game.board(), game.toMove(), moves, kNoIdx);
	if (limits_.maxCandidates > 0 && moves.count > limits_.maxCandidates) {
		moves.count = limits_.maxCandidates;
	}

	Score best = -kScoreInf;
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
		}
		if (best > alpha) {
			alpha = best;
		}
		if (alpha >= beta) {
			++stats_.cutoffs;
			break;
		}
		if (outOfTime()) {
			break;
		}
	}
	return best;
}

// Iterative deepening from the root, publishing a snapshot per completed depth.
SearchResult Engine::search(Game& game, const SearchLimits& limits, const ProgressFn& onProgress) {
	limits_ = limits;
	stop_.store(false, std::memory_order_relaxed);
	start_ = std::chrono::steady_clock::now();
	stats_ = SearchStats{};

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

	orderMoves(game.board(), me, root, kNoIdx);
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
