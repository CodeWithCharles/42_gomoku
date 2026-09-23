/* -------------------------------------------------------------------------- */
/*                            Micro test framework                            */
/* -------------------------------------------------------------------------- */

#include <cstdio>
#include <cstring>
#include <vector>

#include "gomoku/types.hpp"

namespace {

int gFailures = 0;
int gChecks = 0;
const char* gCurrentTest = "";

/* ----------- Saving result of an assertion and signals failures ----------- */
void recordCheck(bool ok, const char* expression, int line) {
	++gChecks;
	if (ok) {
		return;
	}
	++gFailures;
	std::printf("  \033[1;31mFAIL\033[0m %s:%d  %s\n", gCurrentTest, line, expression);
}

#define CHECK(expr) recordCheck((expr), #expr, __LINE__)

struct TestCase {
	const char* name;
	void (*fn)();
};

/* -- Global list of all tests, built before main through Registrar objects - */
std::vector<TestCase>& registry() {
	static std::vector<TestCase> tests;
	return tests;
}

struct Registrar {
	/* ------------ Adds a test to the global list on program's load ------------ */
	Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

#define TEST(name)                                         \
	static void name();                                    \
	static const Registrar registrar_##name(#name, &name); \
	static void name()

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

/* ----------- Starts all tests or only those containing argv[1]. ----------- */
int main(int argc, char** argv) {
	const char* filter = argc > 1 ? argv[1] : nullptr;
	int executed = 0;

	for (const TestCase& test : registry()) {
		if (filter != nullptr && std::strstr(test.name, filter) == nullptr) {
			continue;
		}
		gCurrentTest = test.name;
		const int before = gFailures;
		test.fn();
		++executed;
		if (gFailures == before) {
			std::printf("  \033[1;32m ok \033[0m %s\n", test.name);
		}
	}

	std::printf("\n%d tests, %d assertions, \033[1m%d failure(s)\033[0m\n", executed, gChecks,
				gFailures);
	return gFailures == 0 ? 0 : 1;
}
