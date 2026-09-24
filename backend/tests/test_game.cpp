/* -------------------------------------------------------------------------- */
/*                                 Game tests                                 */
/* -------------------------------------------------------------------------- */

#include "gomoku/game.hpp"
#include "harness.hpp"

namespace {

using namespace gomoku;

// Builds a position through the search path, which performs no arbitration.
// The colour we are not placing is parked on spaced cells of row 0, where it
// can neither line up five nor be captured.
struct Setup {
	Game& game;
	int park = 0;

	// Forces a stone of `p` on (x, y), parking the other colour as needed.
	void put(int x, int y, Player p) {
		PlayedMove scratch;
		while (game.toMove() != p) {
			game.makeMove(idxOf(park, 0), scratch);
			park += 2;
		}
		game.makeMove(idxOf(x, y), scratch);
	}

	// Parks one stone so that `p` gets the move.
	void giveTurnTo(Player p) {
		PlayedMove scratch;
		while (game.toMove() != p) {
			game.makeMove(idxOf(park, 0), scratch);
			park += 2;
		}
	}
};

TEST(new_game_starts_with_black_to_move) {
	Game g;
	CHECK(g.toMove() == Player::Black);
	CHECK(g.status() == GameStatus::Ongoing);
	CHECK(g.pairsTaken(Player::Black) == 0);
	CHECK(g.history().empty());
}

TEST(play_alternates_sides_and_records_history) {
	Game g;
	CHECK(g.play(idxOf(9, 9)) == Legality::Legal);
	CHECK(g.toMove() == Player::White);
	CHECK(g.play(idxOf(9, 10)) == Legality::Legal);
	CHECK(g.toMove() == Player::Black);
	CHECK(g.history().size() == 2);
	CHECK(g.history()[0].idx == idxOf(9, 9));
	CHECK(g.history()[0].player == Player::Black);
}

TEST(play_refuses_an_occupied_cell) {
	Game g;
	g.play(idxOf(9, 9));
	CHECK(g.play(idxOf(9, 9)) == Legality::Occupied);
	CHECK(g.toMove() == Player::White);
}

TEST(play_refuses_out_of_bounds) {
	Game g;
	CHECK(g.play(static_cast<Idx>(-1)) == Legality::OutOfBounds);
	CHECK(g.play(static_cast<Idx>(kCellCount)) == Legality::OutOfBounds);
}

TEST(check_refuses_the_wrong_colour) {
	Game g;
	CHECK(g.check(idxOf(9, 9), Player::White) == Legality::NotYourTurn);
	CHECK(g.check(idxOf(9, 9), Player::Black) == Legality::Legal);
}

TEST(check_leaves_the_game_untouched) {
	Game g;
	Setup s{g};
	s.put(8, 9, Player::Black);
	s.put(10, 9, Player::Black);
	s.put(9, 8, Player::Black);
	s.put(9, 10, Player::Black);
	s.giveTurnTo(Player::Black);

	const uint64_t hash = g.board().hash();
	const Player turn = g.toMove();
	g.check(idxOf(9, 9), Player::Black);
	CHECK(g.board().hash() == hash);
	CHECK(g.toMove() == turn);
	CHECK(g.status() == GameStatus::Ongoing);
}

TEST(play_refuses_a_double_three) {
	Game g;
	Setup s{g};
	s.put(8, 9, Player::Black);
	s.put(10, 9, Player::Black);
	s.put(9, 8, Player::Black);
	s.put(9, 10, Player::Black);
	s.giveTurnTo(Player::Black);
	CHECK(g.play(idxOf(9, 9)) == Legality::DoubleThree);
}

TEST(disabling_double_three_allows_the_move) {
	GameConfig cfg;
	cfg.doubleThreeForbidden = false;
	Game g(cfg);
	Setup s{g};
	s.put(8, 9, Player::Black);
	s.put(10, 9, Player::Black);
	s.put(9, 8, Player::Black);
	s.put(9, 10, Player::Black);
	s.giveTurnTo(Player::Black);
	CHECK(g.play(idxOf(9, 9)) == Legality::Legal);
}

TEST(a_capturing_move_may_create_a_double_three) {
	Game g;
	Setup s{g};
	s.put(8, 9, Player::Black);
	s.put(10, 9, Player::Black);
	s.put(9, 8, Player::Black);
	s.put(9, 10, Player::Black);
	s.put(10, 10, Player::White);
	s.put(11, 11, Player::White);
	s.put(12, 12, Player::Black);
	s.giveTurnTo(Player::Black);

	CHECK(g.play(idxOf(9, 9)) == Legality::Legal);
	CHECK(g.pairsTaken(Player::Black) == 1);
	CHECK(g.board().isEmpty(idxOf(10, 10)));
	CHECK(g.board().isEmpty(idxOf(11, 11)));
}

TEST(capture_removes_the_pair_and_counts_it) {
	Game g;
	Setup s{g};
	s.put(6, 5, Player::White);
	s.put(7, 5, Player::White);
	s.put(8, 5, Player::Black);
	s.giveTurnTo(Player::Black);

	CHECK(g.play(idxOf(5, 5)) == Legality::Legal);
	CHECK(g.pairsTaken(Player::Black) == 1);
	CHECK(g.board().isEmpty(idxOf(6, 5)));
	CHECK(g.history().back().capturedCount == 2);
}

TEST(five_pairs_win_by_capture) {
	Game g;
	Setup s{g};
	for (int row = 2; row <= 10; row += 2) {
		s.put(3, row, Player::White);
		s.put(4, row, Player::White);
		s.put(5, row, Player::Black);
	}
	s.giveTurnTo(Player::Black);

	PlayedMove scratch;
	int filler = 0;
	for (int row = 2; row <= 8; row += 2) {
		g.makeMove(idxOf(2, row), scratch);
		g.makeMove(idxOf(filler, 18), scratch);
		filler += 2;
	}
	CHECK(g.pairsTaken(Player::Black) == 4);
	CHECK(g.status() == GameStatus::Ongoing);

	CHECK(g.play(idxOf(2, 10)) == Legality::Legal);
	CHECK(g.pairsTaken(Player::Black) == 5);
	CHECK(g.status() == GameStatus::BlackWins);
	CHECK(g.winReason() == WinReason::Captures);
}

TEST(an_unbreakable_five_wins) {
	Game g;
	Setup s{g};
	for (int x = 5; x < 9; ++x) {
		s.put(x, 9, Player::Black);
	}
	s.giveTurnTo(Player::Black);

	CHECK(g.play(idxOf(9, 9)) == Legality::Legal);
	CHECK(g.status() == GameStatus::BlackWins);
	CHECK(g.winReason() == WinReason::Alignment);
	CHECK(g.play(idxOf(1, 1)) == Legality::GameOver);
}

TEST(a_breakable_five_does_not_win_yet) {
	Game g;
	Setup s{g};
	for (int x = 5; x < 9; ++x) {
		s.put(x, 9, Player::Black);
	}
	s.put(7, 10, Player::Black);
	s.put(7, 8, Player::White);
	s.giveTurnTo(Player::Black);

	CHECK(g.play(idxOf(9, 9)) == Legality::Legal);
	CHECK(g.status() == GameStatus::Ongoing);
}

TEST(a_five_wins_once_it_cannot_be_broken) {
	Game g;
	Setup s{g};
	for (int x = 5; x < 9; ++x) {
		s.put(x, 9, Player::Black);
	}
	s.put(7, 10, Player::Black);
	s.put(7, 8, Player::White);
	s.giveTurnTo(Player::Black);

	CHECK(g.play(idxOf(9, 9)) == Legality::Legal);
	CHECK(g.status() == GameStatus::Ongoing);

	CHECK(g.play(idxOf(17, 17)) == Legality::Legal);
	CHECK(g.status() == GameStatus::Ongoing);

	CHECK(g.play(idxOf(7, 11)) == Legality::Legal);
	CHECK(g.status() == GameStatus::BlackWins);
	CHECK(g.winReason() == WinReason::Alignment);
}

// The opponent's own move can turn a pending five into a winning one, by
// capturing a pair that was the only way to break it. updateStatus therefore
// has to settle the opponent's alignment before the mover's own.
TEST(a_pending_five_wins_on_the_opponents_move) {
	Game g;
	Setup s{g};
	for (int x = 5; x <= 9; ++x) {
		s.put(x, 9, Player::Black);
	}
	s.put(7, 10, Player::Black);
	s.put(8, 10, Player::Black);
	s.put(7, 8, Player::White);
	s.put(6, 10, Player::White);
	s.giveTurnTo(Player::White);

	CHECK(g.status() == GameStatus::Ongoing);
	CHECK(g.play(idxOf(9, 10)) == Legality::Legal);
	CHECK(g.pairsTaken(Player::White) == 1);
	CHECK(g.status() == GameStatus::BlackWins);
	CHECK(g.winReason() == WinReason::Alignment);
}

// Both sides end up holding an unbreakable five on the same move: White
// completes its own line with the very capture that saves Black's. The five
// that survived the opponent's reply comes first, so Black wins.
TEST(an_older_five_beats_one_completed_on_the_same_move) {
	Game g;
	Setup s{g};
	for (int x = 5; x <= 9; ++x) {
		s.put(x, 9, Player::Black);
	}
	s.put(7, 10, Player::Black);
	s.put(8, 10, Player::Black);
	s.put(7, 8, Player::White);
	s.put(6, 10, Player::White);
	for (int y = 11; y <= 14; ++y) {
		s.put(9, y, Player::White);
	}
	s.giveTurnTo(Player::White);

	CHECK(g.status() == GameStatus::Ongoing);
	CHECK(g.play(idxOf(9, 10)) == Legality::Legal);
	CHECK(g.pairsTaken(Player::White) == 1);
	CHECK(g.status() == GameStatus::BlackWins);
	CHECK(g.winReason() == WinReason::Alignment);
}

// Subject, endgame capture, second branch: the owner of the five already lost
// four pairs and the opponent can still take a fifth, so the opponent wins by
// capture even though the line itself cannot be broken.
TEST(four_pairs_lost_hands_the_win_to_the_capturer) {
	Game g;
	Setup s{g};
	for (int row = 2; row <= 8; row += 2) {
		s.put(3, row, Player::Black);
		s.put(4, row, Player::Black);
		s.put(5, row, Player::White);
	}
	for (int x = 5; x <= 8; ++x) {
		s.put(x, 12, Player::Black);
	}
	s.put(15, 15, Player::Black);
	s.put(15, 16, Player::Black);
	s.put(15, 14, Player::White);
	s.giveTurnTo(Player::White);

	PlayedMove scratch;
	int filler = 0;
	for (int row = 2; row <= 8; row += 2) {
		g.makeMove(idxOf(2, row), scratch);
		g.makeMove(idxOf(filler, 18), scratch);
		filler += 2;
	}
	CHECK(g.pairsTaken(Player::White) == 4);
	CHECK(g.status() == GameStatus::Ongoing);

	s.giveTurnTo(Player::Black);
	CHECK(g.play(idxOf(9, 12)) == Legality::Legal);
	CHECK(g.status() == GameStatus::WhiteWins);
	CHECK(g.winReason() == WinReason::Captures);
}

TEST(make_unmake_is_symmetric) {
	Game g;
	g.play(idxOf(9, 9));
	g.play(idxOf(9, 10));
	const uint64_t hash = g.board().hash();
	const Player turn = g.toMove();

	PlayedMove undo;
	g.makeMove(idxOf(5, 5), undo);
	CHECK(g.board().hash() != hash);
	g.unmakeMove(undo);
	CHECK(g.board().hash() == hash);
	CHECK(g.toMove() == turn);
}

TEST(make_unmake_restores_captured_stones) {
	Game g;
	Setup s{g};
	s.put(6, 5, Player::White);
	s.put(7, 5, Player::White);
	s.put(8, 5, Player::Black);
	s.giveTurnTo(Player::Black);
	const uint64_t hash = g.board().hash();

	PlayedMove undo;
	g.makeMove(idxOf(5, 5), undo);
	CHECK(undo.capturedCount == 2);
	CHECK(g.pairsTaken(Player::Black) == 1);

	g.unmakeMove(undo);
	CHECK(g.board().hash() == hash);
	CHECK(g.pairsTaken(Player::Black) == 0);
	CHECK(g.board().at(idxOf(6, 5)) == Player::White);
	CHECK(g.board().at(idxOf(7, 5)) == Player::White);
}

TEST(undo_takes_back_the_last_move) {
	Game g;
	g.play(idxOf(9, 9));
	const uint64_t afterFirst = g.board().hash();
	g.play(idxOf(9, 10));

	CHECK(g.undo());
	CHECK(g.board().hash() == afterFirst);
	CHECK(g.toMove() == Player::White);
	CHECK(g.history().size() == 1);

	CHECK(g.undo());
	CHECK(g.history().empty());
	CHECK(!g.undo());
}

TEST(terminal_after_sees_a_win) {
	Game g;
	Setup s{g};
	for (int x = 5; x < 9; ++x) {
		s.put(x, 9, Player::Black);
	}
	s.giveTurnTo(Player::Black);

	PlayedMove undo;
	g.makeMove(idxOf(9, 9), undo);
	CHECK(g.terminalAfter(undo) == GameStatus::BlackWins);
	g.unmakeMove(undo);
	CHECK(g.status() == GameStatus::Ongoing);
}

TEST(terminal_after_sees_a_win_by_captures) {
	Game g;
	Setup s{g};
	for (int row = 2; row <= 10; row += 2) {
		s.put(3, row, Player::White);
		s.put(4, row, Player::White);
		s.put(5, row, Player::Black);
	}
	s.giveTurnTo(Player::Black);

	PlayedMove scratch;
	int filler = 0;
	for (int row = 2; row <= 8; row += 2) {
		g.makeMove(idxOf(2, row), scratch);
		g.makeMove(idxOf(filler, 18), scratch);
		filler += 2;
	}
	CHECK(g.pairsTaken(Player::Black) == 4);

	PlayedMove last;
	g.makeMove(idxOf(2, 10), last);
	CHECK(g.pairsTaken(Player::Black) == 5);
	CHECK(g.terminalAfter(last) == GameStatus::BlackWins);
}

TEST(reset_clears_everything) {
	Game g;
	g.play(idxOf(9, 9));
	g.play(idxOf(9, 10));
	g.reset();
	CHECK(g.toMove() == Player::Black);
	CHECK(g.board().stoneCount() == 0);
	CHECK(g.board().hash() == 0);
	CHECK(g.history().empty());
	CHECK(g.status() == GameStatus::Ongoing);
}

}  // namespace
