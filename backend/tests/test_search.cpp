/* -------------------------------------------------------------------------- */
/*                                Search tests                                */
/* -------------------------------------------------------------------------- */

#include "gomoku/eval.hpp"
#include "gomoku/movegen.hpp"
#include "gomoku/search.hpp"
#include "harness.hpp"

namespace {

using namespace gomoku;

// True if `list` holds `i`.
bool contains(const MoveList& list, Idx i) {
	for (int k = 0; k < list.count; ++k) {
		if (list.moves[k] == i) {
			return true;
		}
	}
	return false;
}

// Plays `moves` in order, alternating colours, and reports any refusal.
bool playAll(Game& g, std::initializer_list<Idx> moves) {
	for (const Idx move : moves) {
		if (g.play(move) != Legality::Legal) {
			return false;
		}
	}
	return true;
}

TEST(movegen_opens_on_the_centre) {
	Board b;
	MoveList moves;
	movegen::generate(b, moves);
	CHECK(moves.count == 1);
	CHECK(moves.moves[0] == idxOf(9, 9));
}

TEST(movegen_keeps_only_the_neighbourhood) {
	Board b;
	b.place(idxOf(9, 9), Player::Black);
	MoveList moves;
	movegen::generate(b, moves, 1);
	CHECK(moves.count == 8);
	CHECK(contains(moves, idxOf(8, 8)));
	CHECK(contains(moves, idxOf(10, 10)));
	CHECK(!contains(moves, idxOf(9, 9)));
	CHECK(!contains(moves, idxOf(7, 7)));

	MoveList wider;
	movegen::generate(b, wider, 2);
	CHECK(wider.count == 24);
	CHECK(contains(wider, idxOf(7, 7)));
}

TEST(movegen_skips_occupied_cells) {
	Board b;
	b.place(idxOf(9, 9), Player::Black);
	b.place(idxOf(9, 10), Player::White);
	MoveList moves;
	movegen::generate(b, moves, 1);
	CHECK(!contains(moves, idxOf(9, 9)));
	CHECK(!contains(moves, idxOf(9, 10)));
}

TEST(eval_is_symmetric_between_the_two_sides) {
	Board b;
	for (int x = 5; x < 8; ++x) {
		b.place(idxOf(x, 9), Player::Black);
	}
	b.place(idxOf(12, 12), Player::White);
	const std::array<int, 2> pairs = {0, 0};
	CHECK(eval::evaluate(b, Player::Black, pairs) == -eval::evaluate(b, Player::White, pairs));
}

TEST(eval_rewards_a_longer_run) {
	const std::array<int, 2> pairs = {0, 0};
	Board two;
	two.place(idxOf(5, 9), Player::Black);
	two.place(idxOf(6, 9), Player::Black);

	Board four;
	for (int x = 5; x < 9; ++x) {
		four.place(idxOf(x, 9), Player::Black);
	}
	CHECK(eval::evaluate(four, Player::Black, pairs) > eval::evaluate(two, Player::Black, pairs));
}

TEST(eval_counts_captured_pairs) {
	Board b;
	b.place(idxOf(9, 9), Player::Black);
	const std::array<int, 2> none = {0, 0};
	const std::array<int, 2> taken = {3, 0};
	CHECK(eval::evaluate(b, Player::Black, taken) > eval::evaluate(b, Player::Black, none));
}

TEST(search_returns_a_legal_move) {
	Game g;
	g.play(idxOf(9, 9));

	Engine engine;
	SearchLimits limits;
	limits.maxDepth = 3;
	limits.budget = std::chrono::milliseconds(200);
	const SearchResult result = engine.search(g, limits);

	CHECK(result.best != kNoIdx);
	CHECK(g.check(result.best, g.toMove()) == Legality::Legal);
	CHECK(result.stats.depth >= 1);
	CHECK(result.stats.nodes > 0);
}

TEST(search_leaves_the_game_untouched) {
	Game g;
	CHECK(playAll(g, {idxOf(9, 9), idxOf(9, 10), idxOf(8, 9)}));
	const uint64_t hash = g.board().hash();
	const Player turn = g.toMove();
	const size_t moves = g.history().size();

	Engine engine;
	SearchLimits limits;
	limits.maxDepth = 4;
	limits.budget = std::chrono::milliseconds(200);
	engine.search(g, limits);

	CHECK(g.board().hash() == hash);
	CHECK(g.toMove() == turn);
	CHECK(g.history().size() == moves);
}

TEST(search_takes_the_win_in_one) {
	Game g;
	CHECK(playAll(g, {idxOf(5, 9), idxOf(0, 0), idxOf(6, 9), idxOf(0, 2), idxOf(7, 9), idxOf(0, 4),
					  idxOf(8, 9), idxOf(0, 6)}));
	CHECK(g.toMove() == Player::Black);

	Engine engine;
	SearchLimits limits;
	limits.maxDepth = 4;
	limits.budget = std::chrono::milliseconds(400);
	const SearchResult result = engine.search(g, limits);

	const bool wins = result.best == idxOf(4, 9) || result.best == idxOf(9, 9);
	CHECK(wins);
}

TEST(search_blocks_an_immediate_loss) {
	Game g;
	CHECK(playAll(g, {idxOf(0, 0), idxOf(5, 9), idxOf(0, 2), idxOf(6, 9), idxOf(0, 4), idxOf(7, 9),
					  idxOf(0, 6), idxOf(8, 9)}));
	CHECK(g.toMove() == Player::Black);

	Engine engine;
	SearchLimits limits;
	limits.maxDepth = 4;
	limits.budget = std::chrono::milliseconds(400);
	const SearchResult result = engine.search(g, limits);

	const bool blocks = result.best == idxOf(4, 9) || result.best == idxOf(9, 9);
	CHECK(blocks);
}

// ADR-008: the root filter is the only thing keeping the engine from playing
// a forbidden move, so it deserves a test of its own.
TEST(search_never_returns_a_forbidden_move) {
	Game g;
	CHECK(playAll(g, {idxOf(8, 9), idxOf(0, 0), idxOf(10, 9), idxOf(0, 2), idxOf(9, 8), idxOf(0, 4),
					  idxOf(9, 10), idxOf(0, 6)}));
	CHECK(g.toMove() == Player::Black);
	CHECK(g.check(idxOf(9, 9), Player::Black) == Legality::DoubleThree);

	Engine engine;
	SearchLimits limits;
	limits.maxDepth = 3;
	limits.budget = std::chrono::milliseconds(300);
	const SearchResult result = engine.search(g, limits);

	CHECK(result.best != idxOf(9, 9));
	CHECK(g.check(result.best, Player::Black) == Legality::Legal);
}

TEST(search_records_beta_cutoffs) {
	Game g;
	CHECK(playAll(g, {idxOf(9, 9), idxOf(9, 10), idxOf(8, 9), idxOf(10, 10)}));

	Engine engine;
	SearchLimits limits;
	limits.maxDepth = 4;
	limits.budget = std::chrono::milliseconds(400);
	const SearchResult result = engine.search(g, limits);

	CHECK(result.stats.cutoffs > 0);
	CHECK(result.stats.leaves > 0);
}

TEST(forward_pruning_shrinks_the_tree) {
	Game g;
	CHECK(playAll(g, {idxOf(9, 9), idxOf(9, 10), idxOf(8, 9), idxOf(10, 10)}));

	SearchLimits wide;
	wide.maxDepth = 4;
	wide.budget = std::chrono::milliseconds(2000);
	wide.maxCandidates = 20;

	SearchLimits narrow = wide;
	narrow.maxCandidates = 2;

	Engine first;
	const uint64_t wideNodes = first.search(g, wide).stats.nodes;
	Engine second;
	const uint64_t narrowNodes = second.search(g, narrow).stats.nodes;

	CHECK(narrowNodes < wideNodes);
}

TEST(search_honours_its_time_budget) {
	Game g;
	g.play(idxOf(9, 9));

	Engine engine;
	SearchLimits limits;
	limits.maxDepth = 20;
	limits.budget = std::chrono::milliseconds(80);

	const auto start = std::chrono::steady_clock::now();
	const SearchResult result = engine.search(g, limits);
	const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
							 std::chrono::steady_clock::now() - start)
							 .count();

	CHECK(result.best != kNoIdx);
	CHECK(elapsed < 500);
}

TEST(search_reports_every_root_move_it_scored) {
	Game g;
	CHECK(playAll(g, {idxOf(9, 9), idxOf(9, 10)}));

	Engine engine;
	SearchLimits limits;
	limits.maxDepth = 2;
	limits.budget = std::chrono::milliseconds(300);
	const SearchResult result = engine.search(g, limits);

	CHECK(!result.stats.rootScores.empty());
	CHECK(!result.stats.pv.empty());
	CHECK(result.stats.pv[0] == result.best);
}

}  // namespace
