// doctest first: stdafx.h brings in Xlib on Linux, whose macros (None, Bool, ...) clash with doctest's names
#include "doctest.h"

#include "stdafx.h"

#include "Misc/Input/Bindings/BindingFile.h"

#include <filesystem>

namespace {
std::filesystem::path Data(const char* name)
{
	return std::filesystem::path(OC_TEST_DATA_DIR) / name;
}
} // namespace

TEST_CASE("Binding_file_reads_every_section")
{
	BindingFile f = LoadBindingFile(Data("bindings_all_sections.json"));
	CHECK(f.warnings.empty());
	CHECK(f.controllerType == "knuckles");
	REQUIRE(f.sets.count("/actions/main"));
	const ActionSetBindings& main = f.sets["/actions/main"];

	REQUIRE(main.sources.size() == 2); // the disabled one left out
	const SourceBinding& trigger = main.sources[0];
	CHECK(trigger.path == "/user/hand/right/input/trigger");
	CHECK(trigger.mode == "trigger");
	CHECK(trigger.parameters.at("click_activate_threshold") == "0.8");
	CHECK(trigger.parameters.at("click_deactivate_threshold") == "0.6");
	CHECK(trigger.parameters.at("haptic_on_click") == "true");
	CHECK(trigger.inputs.at("pull") == "/actions/main/in/throttle");
	CHECK(trigger.inputs.at("click") == "/actions/main/in/fire");
	CHECK(main.sources[1].parameters.at("sub_mode") == "click");

	REQUIRE(main.poses.size() == 1);
	CHECK(main.poses[0].output == "/actions/main/in/hand_left");
	CHECK(main.poses[0].path == "/user/hand/left/pose/raw");
	REQUIRE(main.haptics.size() == 1);
	CHECK(main.haptics[0].parameters.at("amplitude") == "0.5");
	REQUIRE(main.skeleton.size() == 1);
	CHECK(main.skeleton[0].path == "/user/hand/left/input/skeleton/left");
	REQUIRE(main.chords.size() == 1);
	CHECK(main.chords[0].output == "/actions/main/in/both");
	REQUIRE(main.chords[0].inputs.size() == 2);
	CHECK(main.chords[0].inputs[1] == std::make_pair(std::string("/user/hand/right/input/a"), std::string("click")));

	CHECK(f.sets["/actions/menu"].sources.size() == 1);
	REQUIRE(f.simulatedActions.size() == 1);
	CHECK(f.simulatedActions[0].name == "/actions/main/in/fire");
	CHECK(f.simulatedActions[0].type == "click");
}

TEST_CASE("Binding_file_with_bom_comments_trailing_commas")
{
	BindingFile f = LoadBindingFile(Data("bindings_bom_comments.json"));
	CHECK(f.warnings.empty());
	CHECK(f.controllerType == "oculus_touch");
	REQUIRE(f.sets["/actions/main"].sources.size() == 1);
	CHECK(f.sets["/actions/main"].sources[0].inputs.at("click") == "/actions/main/in/jump");
}

TEST_CASE("Binding_file_unknown_section_warns")
{
	BindingFile f = ParseBindingFile(R"({ "controller_type": "knuckles", "bindings": { "/actions/main": {
		"sources": [ { "path": "/user/hand/right/input/a", "mode": "button", "frobnicate": 1, "inputs": { "click": { "output": "/actions/main/in/a" } } } ],
		"gestures": [] } }, "colour": "blue" })");
	CHECK(f.warnings.size() == 3); // the unknown source field, the unknown section, the unknown top-level field
	CHECK(f.sets["/actions/main"].sources.size() == 1); // the source itself is kept
}

TEST_CASE("Binding_file_disabled_not_a_boolean_warns")
{
	// A string isn't a boolean JsonCpp converts (asBool would throw): the source is kept, with a warning
	BindingFile f = ParseBindingFile(R"({ "bindings": { "/actions/main": { "sources": [
		{ "path": "/user/hand/right/input/a", "mode": "button", "disabled": "true", "inputs": { "click": { "output": "/actions/main/in/a" } } },
		{ "path": "/user/hand/right/input/b", "mode": "button", "disabled": 1, "inputs": { "click": { "output": "/actions/main/in/b" } } } ] } } })");
	CHECK(f.warnings.size() == 1);
	REQUIRE(f.sets["/actions/main"].sources.size() == 1);
	CHECK(f.sets["/actions/main"].sources[0].path == "/user/hand/right/input/a");
}

TEST_CASE("Binding_file_missing_bindings_is_empty_with_warning")
{
	BindingFile f = ParseBindingFile(R"({ "controller_type": "vive_controller" })");
	CHECK(f.sets.empty());
	CHECK(f.warnings.size() == 1);
	CHECK(ParseBindingFile("not json at all").warnings.size() == 1);
	CHECK(ParseBindingFile("[1, 2]").warnings.size() == 1);
	CHECK(LoadBindingFile(Data("no_such_bindings.json")).warnings.size() == 1);
}

TEST_CASE("Binding_paths_lower_cased")
{
	BindingFile f = ParseBindingFile(R"({ "bindings": { "/Actions/Main": { "sources": [
		{ "path": "/User/Hand/Right/Input/A", "mode": "Button", "inputs": { "Click": { "output": "/Actions/Main/In/Jump" } } } ] } } })");
	REQUIRE(f.sets.count("/actions/main"));
	const SourceBinding& s = f.sets["/actions/main"].sources.at(0);
	CHECK(s.path == "/user/hand/right/input/a");
	CHECK(s.mode == "button");
	CHECK(s.inputs.at("click") == "/actions/main/in/jump");
}
