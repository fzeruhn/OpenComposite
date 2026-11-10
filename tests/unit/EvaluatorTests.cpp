// doctest first: stdafx.h brings in Xlib on Linux, whose macros (None, Bool, ...) clash with doctest's names
#include "doctest.h"

#include "stdafx.h"

#include "Misc/Input/Bindings/Evaluator.h"

namespace {
const std::string kA = "/user/hand/right/input/a", kB = "/user/hand/right/input/b", kLeftA = "/user/hand/left/input/a";

// Doubles, so the tests' whole numbers (0, 1) need no conversion warning
RawInputState Raw(std::map<std::string, double> values, double time = 0)
{
	RawInputState in;
	in.time = time;
	for (auto& [k, v] : values)
		in.value[k] = static_cast<float>(v);
	return in;
}
} // namespace

TEST_CASE("Two_bindings_one_action_or")
{
	BindingFile f = ParseBindingFile(R"({ "bindings": { "/actions/main": { "sources": [
		{ "path": "/user/hand/right/input/a", "mode": "button", "inputs": { "click": { "output": "/actions/main/in/jump" } } },
		{ "path": "/user/hand/right/input/b", "mode": "button", "inputs": { "click": { "output": "/actions/main/in/jump" } } } ] } } })");
	BindingEvaluator e(f);
	e.SetActiveActionSets({ { "/actions/main", 0 } });
	e.Update(Raw({ { kA + "/click", 0 }, { kB + "/click", 0 } }, 0.0));
	CHECK_FALSE(e.Outputs().digital.at("/actions/main/in/jump").state);
	e.Update(Raw({ { kA + "/click", 0 }, { kB + "/click", 1 } }, 0.1));
	CHECK(e.Outputs().digital.at("/actions/main/in/jump").state);
	CHECK(e.Outputs().digital.at("/actions/main/in/jump").changed);
	CHECK(e.Outputs().digital.at("/actions/main/in/jump").origin == kB);
	e.Update(Raw({ { kA + "/click", 1 }, { kB + "/click", 1 } }, 0.2));
	CHECK(e.Outputs().digital.at("/actions/main/in/jump").state);
	CHECK_FALSE(e.Outputs().digital.at("/actions/main/in/jump").changed); // still on: no change
	CHECK(e.Outputs().digital.at("/actions/main/in/jump").changeTime == doctest::Approx(0.1));
}

TEST_CASE("Analog_largest_magnitude")
{
	BindingFile f = ParseBindingFile(R"({ "bindings": { "/actions/main": { "sources": [
		{ "path": "/user/hand/right/input/trigger", "mode": "trigger", "inputs": { "pull": { "output": "/actions/main/in/throttle" } } },
		{ "path": "/user/hand/left/input/trigger", "mode": "trigger", "inputs": { "pull": { "output": "/actions/main/in/throttle" } } } ] } } })");
	BindingEvaluator e(f);
	e.SetActiveActionSets({ { "/actions/main", 0 } });
	e.Update(Raw({ { "/user/hand/right/input/trigger/value", 0.3f }, { "/user/hand/left/input/trigger/value", 0.8f } }));
	CHECK(e.Outputs().analog.at("/actions/main/in/throttle").x == 0.8f);
	CHECK(e.Outputs().analog.at("/actions/main/in/throttle").origin == "/user/hand/left/input/trigger");
}

TEST_CASE("Chord_needs_all_inputs")
{
	BindingFile f = ParseBindingFile(R"({ "bindings": { "/actions/main": {
		"sources": [ { "path": "/user/hand/right/input/a", "mode": "button", "inputs": {} },
		             { "path": "/user/hand/left/input/a", "mode": "button", "inputs": {} } ],
		"chords": [ { "output": "/actions/main/in/both", "inputs": [ [ "/user/hand/right/input/a", "click" ], [ "/user/hand/left/input/a", "click" ] ] } ] } } })");
	BindingEvaluator e(f);
	e.SetActiveActionSets({ { "/actions/main", 0 } });
	e.Update(Raw({ { kA + "/click", 1 }, { kLeftA + "/click", 0 } }));
	CHECK_FALSE(e.Outputs().digital.at("/actions/main/in/both").state);
	e.Update(Raw({ { kA + "/click", 1 }, { kLeftA + "/click", 1 } }));
	CHECK(e.Outputs().digital.at("/actions/main/in/both").state);
	e.Update(Raw({ { kA + "/click", 0 }, { kLeftA + "/click", 1 } }));
	CHECK_FALSE(e.Outputs().digital.at("/actions/main/in/both").state);
}

TEST_CASE("Higher_priority_set_blocks_lower")
{
	BindingFile f = ParseBindingFile(R"({ "bindings": {
		"/actions/main": { "sources": [ { "path": "/user/hand/right/input/a", "mode": "button", "inputs": { "click": { "output": "/actions/main/in/jump" } } },
		                                { "path": "/user/hand/right/input/b", "mode": "button", "inputs": { "click": { "output": "/actions/main/in/crouch" } } } ] },
		"/actions/menu": { "sources": [ { "path": "/user/hand/right/input/a", "mode": "button", "inputs": { "click": { "output": "/actions/menu/in/select" } } } ] } } })");
	BindingEvaluator e(f);
	e.SetActiveActionSets({ { "/actions/main", 0 }, { "/actions/menu", 10 } });
	e.Update(Raw({ { kA + "/click", 1 }, { kB + "/click", 1 } }));
	CHECK(e.Outputs().digital.at("/actions/menu/in/select").state);
	CHECK(e.Outputs().digital.count("/actions/main/in/jump") == 0); // blocked: the menu reads the a button
	CHECK(e.Outputs().digital.at("/actions/main/in/crouch").state); // b isn't the menu's

	// The same priority: both
	e.SetActiveActionSets({ { "/actions/main", 0 }, { "/actions/menu", 0 } });
	e.Update(Raw({ { kA + "/click", 1 } }));
	CHECK(e.Outputs().digital.at("/actions/main/in/jump").state);
	CHECK(e.Outputs().digital.at("/actions/menu/in/select").state);
}

TEST_CASE("Inactive_set_outputs_nothing")
{
	BindingFile f = ParseBindingFile(R"({ "bindings": { "/actions/main": { "sources": [
		{ "path": "/user/hand/right/input/a", "mode": "button", "inputs": { "click": { "output": "/actions/main/in/jump" } } } ] } } })");
	BindingEvaluator e(f);
	e.Update(Raw({ { kA + "/click", 1 } }));
	CHECK(e.Outputs().digital.empty());
	e.SetActiveActionSets({ { "/actions/other", 0 } });
	e.Update(Raw({ { kA + "/click", 1 } }));
	CHECK(e.Outputs().digital.empty());
	CHECK(e.RawInputs().size() == 4); // its click, value, force and touch
}

TEST_CASE("Haptic_amplitude_scaled")
{
	BindingFile f = ParseBindingFile(R"({ "bindings": { "/actions/main": { "haptics": [
		{ "output": "/actions/main/out/Buzz", "path": "/user/hand/right/output/haptic", "parameters": { "amplitude": "0.25" } },
		{ "output": "/actions/main/out/plain", "path": "/user/hand/left/output/haptic" } ] } } })");
	BindingEvaluator e(f);
	CHECK_FALSE(e.HapticFor("/actions/main/out/buzz").has_value()); // its set isn't active
	e.SetActiveActionSets({ { "/actions/main", 0 } });
	auto buzz = e.HapticFor("/actions/main/out/buzz");
	REQUIRE(buzz.has_value());
	CHECK(buzz->path == "/user/hand/right/output/haptic");
	CHECK(buzz->amplitudeScale == 0.25f);
	CHECK(e.HapticFor("/actions/main/out/plain")->amplitudeScale == 1.0f);
	CHECK_FALSE(e.HapticFor("/actions/main/out/none").has_value());
}

TEST_CASE("Simulated_action_fires")
{
	BindingFile f = ParseBindingFile(R"({ "bindings": {}, "simulated_actions": [
		{ "name": "/actions/main/in/always", "type": "boolean" },
		{ "name": "/actions/main/in/half", "type": "vector1", "value": 0.5 } ] })");
	BindingEvaluator e(f);
	e.Update(Raw({}));
	CHECK(e.Outputs().digital.at("/actions/main/in/always").state);
	CHECK(e.Outputs().analog.at("/actions/main/in/half").x == 0.5f);
}
