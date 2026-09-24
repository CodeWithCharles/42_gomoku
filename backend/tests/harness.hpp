/* -------------------------------------------------------------------------- */
/*                              Tiny test harness                             */
/* -------------------------------------------------------------------------- */

#pragma once

namespace gomoku::test {

bool recordCheck(bool ok, const char* expression, int line);

bool registerTest(const char* name, void (*fn)());
}  // namespace gomoku::test

#define CHECK(expr) gomoku::test::recordCheck((expr), #expr, __LINE__)

#define TEST(name)                                                                  \
	static void name();                                                             \
	static const bool registered_##name = gomoku::test::registerTest(#name, &name); \
	static void name()
