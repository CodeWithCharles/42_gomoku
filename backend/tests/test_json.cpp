/* -------------------------------------------------------------------------- */
/*                                 Json tests                                 */
/* -------------------------------------------------------------------------- */

#include <nlohmann/json.hpp>

#include "harness.hpp"

namespace {

using nlohmann::json;

// Smoke test of the vendored library: if this links and passes, the include
// path and the vendoring are right. The protocol itself is covered by the
// session tests.
TEST(json_library_reads_a_protocol_command) {
	const json command = json::parse(R"({"type":"play","idx":180})");
	CHECK(command.at("type").get<std::string>() == "play");
	CHECK(command.at("idx").get<int>() == 180);
	CHECK(command.contains("type"));
	CHECK(!command.contains("missing"));
	CHECK(command.value("missing", -1) == -1);
}

TEST(json_library_writes_a_state_message) {
	json state;
	state["type"] = "state";
	state["size"] = 19;
	state["pairs"] = {{"black", 1}, {"white", 0}};
	state["pv"] = {180, 199};

	const json back = json::parse(state.dump());
	CHECK(back.at("size").get<int>() == 19);
	CHECK(back.at("pairs").at("black").get<int>() == 1);
	CHECK(back.at("pv").size() == 2);
	CHECK(back.at("pv")[1].get<int>() == 199);
}

TEST(json_library_never_throws_on_malformed_input) {
	const json bad = json::parse("{ oops", nullptr, false);
	CHECK(bad.is_discarded());
}

}  // namespace
