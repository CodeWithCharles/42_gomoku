/* -------------------------------------------------------------------------- */
/*                                Harness code                                */
/* -------------------------------------------------------------------------- */

#include "harness.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

namespace gomoku::test {

namespace {
struct TestCase {
	const char* name;
	void (*fn)();
};

int gFailures = 0;
int gChecks = 0;
const char* gCurrentTest = "";

std::vector<TestCase>& registry() {
	static std::vector<TestCase> tests;
	return tests;
}

}  // namespace

bool recordCheck(bool ok, const char* expression, int line) {
	++gChecks;
	if (!ok) {
		++gFailures;
		std::printf("  \033[1;31mFAIL\033[0m %s:%d  %s\n", gCurrentTest, line, expression);
	}
	return ok;
}

bool registerTest(const char* name, void (*fn)()) {
	registry().push_back({name, fn});
	return true;
}

int runAll(const char* filter) {
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

}  // namespace gomoku::test

int main(int argc, char** argv) {
	return gomoku::test::runAll(argc > 1 ? argv[1] : nullptr);
}
