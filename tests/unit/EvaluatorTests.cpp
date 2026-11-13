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
	// Held, then one hand's inputs gone (no input focus, a controller off): inactive, not still held
	e.Update(Raw({ { kA + "/click", 1 }, { kLeftA + "/click", 1 } }));
	e.Update(Raw({ { kLeftA + "/click", 1 } }));
	CHECK(e.Outputs().digital.count("/actions/main/in/both") == 0);
}

TEST_CASE("Chord_on_a_source_with_a_mode_that_cant_run")
{
	// Two modes on the grip; the first has none of its inputs on this controller (a click-only grip), the grab does
	BindingFile f = ParseBindingFile(R"({ "bindings": { "/actions/main": {
		"sources": [ { "path": "/user/hand/right/input/grip", "mode": "force_sensor", "inputs": {} },
		             { "path": "/user/hand/right/input/grip", "mode": "grab", "inputs": {} },
		             { "path": "/user/hand/right/input/a", "mode": "button", "inputs": {} } ],
		"chords": [ { "output": "/actions/main/in/both", "inputs": [ [ "/user/hand/right/input/grip", "grab" ], [ "/user/hand/right/input/a", "click" ] ] } ] } } })");
	BindingEvaluator e(f);
	e.SetActiveActionSets({ { "/actions/main", 0 } });
	e.Update(Raw({ { "/user/hand/right/input/grip/click", 1 }, { kA + "/click", 1 } }));
	REQUIRE(e.Outputs().digital.count("/actions/main/in/both") == 1);
	CHECK(e.Outputs().digital.at("/actions/main/in/both").state);
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
	CHECK(e.HapticsFor("/actions/main/out/buzz").empty()); // its set isn't active
	e.SetActiveActionSets({ { "/actions/main", 0 } });
	auto buzz = e.HapticsFor("/actions/main/out/buzz");
	REQUIRE(buzz.size() == 1);
	CHECK(buzz[0].path == "/user/hand/right/output/haptic");
	CHECK(buzz[0].amplitudeScale == 0.25f);
	CHECK(e.HapticsFor("/actions/main/out/plain").at(0).amplitudeScale == 1.0f);
	CHECK(e.HapticsFor("/actions/main/out/none").empty());
}

TEST_CASE("Haptic_bound_on_both_hands")
{
	// The Unity plugin's layout: one haptic action, a binding per hand
	BindingFile f = ParseBindingFile(R"({ "bindings": { "/actions/default": { "haptics": [
		{ "output": "/actions/default/out/haptic", "path": "/user/hand/left/output/haptic" },
		{ "output": "/actions/default/out/haptic", "path": "/user/hand/right/output/haptic" } ] } } })");
	BindingEvaluator e(f);
	e.SetActiveActionSets({ { "/actions/default", 0 } });
	auto targets = e.HapticsFor("/actions/default/out/haptic");
	REQUIRE(targets.size() == 2);
	CHECK(targets[0].path == "/user/hand/left/output/haptic");
	CHECK(targets[1].path == "/user/hand/right/output/haptic");
	// A set restricted to one hand gives only that hand's
	e.SetActiveActionSets({ { "/actions/default", 0, "/user/hand/right" } });
	targets = e.HapticsFor("/actions/default/out/haptic");
	REQUIRE(targets.size() == 1);
	CHECK(targets[0].path == "/user/hand/right/output/haptic");
}

TEST_CASE("Restricted_set_reads_only_its_hand")
{
	// One set activated per hand (the Unity plugin's ActionSet.Activate(source)): the left hand's activation doesn't
	// make the right hand's buttons fire it, and blocks nothing on the right
	BindingFile f = ParseBindingFile(R"({ "bindings": {
		"/actions/high": { "sources": [
			{ "path": "/user/hand/left/input/a", "mode": "button", "inputs": { "click": { "output": "/actions/high/in/fire" } } },
			{ "path": "/user/hand/right/input/a", "mode": "button", "inputs": { "click": { "output": "/actions/high/in/fire" } } } ] },
		"/actions/low": { "sources": [
			{ "path": "/user/hand/right/input/a", "mode": "button", "inputs": { "click": { "output": "/actions/low/in/jump" } } } ] } } })");
	BindingEvaluator e(f);
	e.SetActiveActionSets({ { "/actions/high", 1, "/user/hand/left" }, { "/actions/low", 0 } });
	e.Update(Raw({ { kA + "/click", 1 }, { kLeftA + "/click", 0 } }));
	CHECK_FALSE(e.Outputs().digital.at("/actions/high/in/fire").state); // only its left binding is active
	CHECK(e.Outputs().digital.at("/actions/high/in/fire").origin == kLeftA);
	CHECK(e.Outputs().digital.at("/actions/low/in/jump").state); // the right a isn't claimed by the left-only set
	e.Update(Raw({ { kA + "/click", 0 }, { kLeftA + "/click", 1 } }, 0.1));
	CHECK(e.Outputs().digital.at("/actions/high/in/fire").state);
}

TEST_CASE("Both_hands_bound_read_per_hand")
{
	// A read restricted to one hand (ulRestrictToDevice) sees that hand's bindings only: the other hand's press
	// neither turns it off nor changes it, and each trigger reads its own value
	BindingFile f = ParseBindingFile(R"({ "bindings": { "/actions/main": { "sources": [
		{ "path": "/user/hand/left/input/a", "mode": "button", "inputs": { "click": { "output": "/actions/main/in/jump" } } },
		{ "path": "/user/hand/right/input/a", "mode": "button", "inputs": { "click": { "output": "/actions/main/in/jump" } } },
		{ "path": "/user/hand/left/input/trigger", "mode": "trigger", "inputs": { "pull": { "output": "/actions/main/in/throttle" } } },
		{ "path": "/user/hand/right/input/trigger", "mode": "trigger", "inputs": { "pull": { "output": "/actions/main/in/throttle" } } } ] } } })");
	const std::string kLeft = "/user/hand/left", kRight = "/user/hand/right";
	const std::string kLeftTrigger = "/user/hand/left/input/trigger/value", kRightTrigger = "/user/hand/right/input/trigger/value";
	BindingEvaluator e(f);
	e.SetActiveActionSets({ { "/actions/main", 0 } });

	// Idle: both hands active, off
	e.Update(Raw({ { kA + "/click", 0 }, { kLeftA + "/click", 0 }, { kLeftTrigger, 0 }, { kRightTrigger, 0 } }, 0.0));
	REQUIRE(e.OutputsOn(kLeft));
	REQUIRE(e.OutputsOn(kRight));
	CHECK_FALSE(e.OutputsOn(kLeft)->digital.at("/actions/main/in/jump").state);
	CHECK_FALSE(e.OutputsOn(kRight)->digital.at("/actions/main/in/jump").state);
	CHECK(e.OutputsOn("") == nullptr);

	// The left pressed: the right stays off and unchanged
	e.Update(Raw({ { kA + "/click", 0 }, { kLeftA + "/click", 1 }, { kLeftTrigger, 0.8 }, { kRightTrigger, 0 } }, 0.1));
	CHECK(e.Outputs().digital.at("/actions/main/in/jump").state);
	CHECK(e.OutputsOn(kLeft)->digital.at("/actions/main/in/jump").state);
	CHECK(e.OutputsOn(kLeft)->digital.at("/actions/main/in/jump").changed);
	CHECK_FALSE(e.OutputsOn(kRight)->digital.at("/actions/main/in/jump").state);
	CHECK_FALSE(e.OutputsOn(kRight)->digital.at("/actions/main/in/jump").changed);
	CHECK(e.OutputsOn(kRight)->digital.at("/actions/main/in/jump").origin == kA);
	CHECK(e.OutputsOn(kLeft)->analog.at("/actions/main/in/throttle").x == 0.8f);
	CHECK(e.OutputsOn(kRight)->analog.at("/actions/main/in/throttle").x == 0.0f);

	// Both pressed, both triggers pulled: each hand its own change time and value, the smaller trigger too
	e.Update(Raw({ { kA + "/click", 1 }, { kLeftA + "/click", 1 }, { kLeftTrigger, 0.8 }, { kRightTrigger, 0.3 } }, 0.2));
	CHECK(e.OutputsOn(kRight)->digital.at("/actions/main/in/jump").state);
	CHECK(e.OutputsOn(kRight)->digital.at("/actions/main/in/jump").changed);
	CHECK(e.OutputsOn(kRight)->digital.at("/actions/main/in/jump").changeTime == doctest::Approx(0.2));
	CHECK_FALSE(e.OutputsOn(kLeft)->digital.at("/actions/main/in/jump").changed);
	CHECK(e.OutputsOn(kLeft)->digital.at("/actions/main/in/jump").changeTime == doctest::Approx(0.1));
	CHECK(e.OutputsOn(kRight)->analog.at("/actions/main/in/throttle").x == 0.3f);
	CHECK(e.OutputsOn(kRight)->analog.at("/actions/main/in/throttle").dx == doctest::Approx(0.3));
	CHECK(e.OutputsOn(kLeft)->analog.at("/actions/main/in/throttle").x == 0.8f);
	CHECK(e.OutputsOn(kLeft)->analog.at("/actions/main/in/throttle").dx == doctest::Approx(0.0));
	CHECK(e.Outputs().analog.at("/actions/main/in/throttle").x == 0.8f);
}

TEST_CASE("Unrestricted_read_names_the_hand_that_is_on")
{
	// The right hand's source comes first; the left is held, the right tapped after it: on, from the left
	BindingFile f = ParseBindingFile(R"({ "bindings": { "/actions/main": { "sources": [
		{ "path": "/user/hand/right/input/a", "mode": "button", "inputs": { "click": { "output": "/actions/main/in/jump" } } },
		{ "path": "/user/hand/left/input/a", "mode": "button", "inputs": { "click": { "output": "/actions/main/in/jump" } } } ] } } })");
	BindingEvaluator e(f);
	e.SetActiveActionSets({ { "/actions/main", 0 } });
	e.Update(Raw({ { kA + "/click", 0 }, { kLeftA + "/click", 1 } }, 0.1));
	e.Update(Raw({ { kA + "/click", 1 }, { kLeftA + "/click", 1 } }, 0.2));
	e.Update(Raw({ { kA + "/click", 0 }, { kLeftA + "/click", 1 } }, 0.3));
	CHECK(e.Outputs().digital.at("/actions/main/in/jump").state);
	CHECK(e.Outputs().digital.at("/actions/main/in/jump").origin == kLeftA);
}

TEST_CASE("Bindings_without_their_raw_inputs_read_inactive")
{
	// A hand whose controller has none of a binding's inputs (another controller, or no input focus): no output
	BindingFile f = ParseBindingFile(R"({ "bindings": { "/actions/main": { "sources": [
		{ "path": "/user/hand/left/input/a", "mode": "button", "inputs": { "click": { "output": "/actions/main/in/jump" } } },
		{ "path": "/user/hand/right/input/a", "mode": "button", "inputs": { "click": { "output": "/actions/main/in/jump" } } } ] } } })");
	BindingEvaluator e(f);
	e.SetActiveActionSets({ { "/actions/main", 0 } });
	e.Update(Raw({ { kA + "/click", 0 } }, 0.1));
	CHECK(e.OutputsOn("/user/hand/left") == nullptr);
	REQUIRE(e.OutputsOn("/user/hand/right"));
	CHECK(e.OutputsOn("/user/hand/right")->digital.count("/actions/main/in/jump") == 1);
	e.Update(Raw({}, 0.2));
	CHECK(e.Outputs().digital.count("/actions/main/in/jump") == 0);
}

TEST_CASE("Simulated_action_fires")
{
	BindingFile f = ParseBindingFile(R"({ "bindings": {}, "simulated_actions": [
		{ "name": "/actions/main/in/always", "type": "boolean" },
		{ "name": "/actions/main/in/half", "type": "vector1", "value": 0.5 },
		{ "name": "/actions/main/in/off", "type": "boolean", "value": false },
		{ "name": "/actions/main/in/text", "type": "boolean", "value": "false" } ] })");
	BindingEvaluator e(f);
	e.Update(Raw({}));
	CHECK(e.Outputs().digital.at("/actions/main/in/always").state);
	CHECK_FALSE(e.Outputs().digital.at("/actions/main/in/off").state);
	CHECK(e.Outputs().digital.at("/actions/main/in/text").state); // not a boolean: as if no value
	CHECK(e.Outputs().analog.at("/actions/main/in/half").x == 0.5f);
}
