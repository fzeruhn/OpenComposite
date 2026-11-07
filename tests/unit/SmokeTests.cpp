// doctest first: stdafx.h brings in Xlib on Linux, whose macros (None, Bool, ...) clash with doctest's names
#include "doctest.h"

#include "stdafx.h"

#include "Misc/Config.h"
#include "Misc/Input/IndexControllerInteractionProfile.h"
#include "logging.h"

#include <string>
#include <vector>

TEST_CASE("Index profile has its A button")
{
	IndexControllerInteractionProfile p;
	CHECK(p.IsInputPathValid("/user/hand/right/input/a/click"));
}

TEST_CASE("Config defaults")
{
	Config c;
	CHECK(c.SupersampleRatio() == 1.0f);
	CHECK_FALSE(c.StopOnSoftAbort());
}

static std::vector<std::string> sink_lines;
static void test_sink(const char* line) { sink_lines.emplace_back(line); }

TEST_CASE("Log sink sees lines")
{
	sink_lines.clear();
	oovr_log_set_test_sink(test_sink);
	OOVR_LOG("hello-sink");
	oovr_log_set_test_sink(nullptr);

	bool seen = false;
	for (const std::string& l : sink_lines)
		seen = seen || l.find("hello-sink") != std::string::npos;
	CHECK(seen);
}
