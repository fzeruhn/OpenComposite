// doctest first: stdafx.h brings in Xlib on Linux, whose macros (None, Bool, ...) clash with doctest's names
#include "doctest.h"

#include "stdafx.h"

#include "Misc/Input/Bindings/Conversion.h"

namespace {
BindingFile Touch()
{
	return ParseBindingFile(R"({ "controller_type": "oculus_touch", "bindings": { "/actions/main": {
		"sources": [
			{ "path": "/user/hand/left/input/x", "mode": "button", "inputs": { "click": { "output": "/actions/main/in/left_primary" } } },
			{ "path": "/user/hand/left/input/y", "mode": "button", "inputs": { "click": { "output": "/actions/main/in/left_secondary" } } },
			{ "path": "/user/hand/right/input/a", "mode": "button", "inputs": { "click": { "output": "/actions/main/in/right_primary" } } },
			{ "path": "/user/hand/right/input/b", "mode": "button", "inputs": { "click": { "output": "/actions/main/in/right_secondary" } } },
			{ "path": "/user/hand/right/input/joystick", "mode": "joystick", "inputs": { "position": { "output": "/actions/main/in/move" } } } ],
		"chords": [ { "output": "/actions/main/in/both", "inputs": [ [ "/user/hand/left/input/x", "click" ], [ "/user/hand/right/input/a", "click" ] ] } ] } } })");
}
const SourceBinding* Bound(const BindingFile& f, const std::string& action)
{
	for (const auto& [name, set] : f.sets)
		for (const SourceBinding& s : set.sources)
			for (const auto& [input, output] : s.inputs)
				if (output == action)
					return &s;
	return nullptr;
}
} // namespace

TEST_CASE("Touch_to_index_buttons_by_hand")
{
	BindingFile index = ConvertBindingFile(Touch(), "knuckles");
	CHECK(index.controllerType == "knuckles");
	CHECK(Bound(index, "/actions/main/in/left_primary")->path == "/user/hand/left/input/a");
	CHECK(Bound(index, "/actions/main/in/left_secondary")->path == "/user/hand/left/input/b");
	CHECK(Bound(index, "/actions/main/in/right_primary")->path == "/user/hand/right/input/a");
	CHECK(Bound(index, "/actions/main/in/right_secondary")->path == "/user/hand/right/input/b");
	CHECK(Bound(index, "/actions/main/in/move")->path == "/user/hand/right/input/thumbstick");
	REQUIRE(index.sets["/actions/main"].chords.size() == 1);
	CHECK(index.sets["/actions/main"].chords[0].inputs[0].first == "/user/hand/left/input/a");
	CHECK(index.warnings.empty());

	// And back
	BindingFile touch = ConvertBindingFile(index, "oculus_touch");
	CHECK(Bound(touch, "/actions/main/in/left_primary")->path == "/user/hand/left/input/x");
	CHECK(Bound(touch, "/actions/main/in/right_secondary")->path == "/user/hand/right/input/b");
}

TEST_CASE("Vive_trackpad_to_index_thumbstick")
{
	BindingFile vive = ParseBindingFile(R"({ "controller_type": "vive_controller", "bindings": { "/actions/main": { "sources": [
		{ "path": "/user/hand/left/input/trackpad", "mode": "trackpad", "inputs": { "position": { "output": "/actions/main/in/move" } } },
		{ "path": "/user/hand/right/input/trackpad", "mode": "dpad", "parameters": { "sub_mode": "click" },
		  "inputs": { "north": { "output": "/actions/main/in/jump" } } },
		{ "path": "/user/hand/right/input/application_menu", "mode": "button", "inputs": { "click": { "output": "/actions/main/in/menu" } } } ] } } })");
	BindingFile index = ConvertBindingFile(vive, "knuckles");
	const SourceBinding* move = Bound(index, "/actions/main/in/move");
	REQUIRE(move);
	CHECK(move->path == "/user/hand/left/input/thumbstick");
	CHECK(move->mode == "joystick");
	const SourceBinding* jump = Bound(index, "/actions/main/in/jump");
	REQUIRE(jump);
	CHECK(jump->path == "/user/hand/right/input/thumbstick");
	CHECK(jump->mode == "dpad");
	CHECK(jump->parameters.at("sub_mode") == "click");
	CHECK(Bound(index, "/actions/main/in/menu")->path == "/user/hand/right/input/system"); // Index has no menu button
}

TEST_CASE("Index_to_vive_drops_b_with_warning")
{
	BindingFile index = ParseBindingFile(R"({ "controller_type": "knuckles", "bindings": { "/actions/main": { "sources": [
		{ "path": "/user/hand/right/input/b", "mode": "button", "inputs": { "click": { "output": "/actions/main/in/back" } } },
		{ "path": "/user/hand/right/input/thumbstick", "mode": "joystick", "inputs": { "position": { "output": "/actions/main/in/move" } } } ] } } })");
	BindingFile vive = ConvertBindingFile(index, "vive_controller");
	CHECK(Bound(vive, "/actions/main/in/back") == nullptr);
	REQUIRE(vive.warnings.size() == 1);
	CHECK(vive.warnings[0].find("/user/hand/right/input/b") != std::string::npos);
	const SourceBinding* move = Bound(vive, "/actions/main/in/move");
	REQUIRE(move);
	CHECK(move->path == "/user/hand/right/input/trackpad");
	CHECK(move->mode == "trackpad");
}

TEST_CASE("Conversion_keeps_action_outputs")
{
	BindingFile touch = Touch();
	for (const char* type : { "knuckles", "vive_controller", "holographic_controller", "hpmotioncontroller", "generic", "khr_simple_controller" }) {
		BindingFile converted = ConvertBindingFile(touch, type);
		for (const auto& [name, set] : converted.sets) {
			for (const SourceBinding& s : set.sources) {
				// Every output is one the original file has
				for (const auto& [input, output] : s.inputs)
					CHECK(Bound(touch, output) != nullptr);
			}
		}
	}
	// The same controller: as it was
	BindingFile same = ConvertBindingFile(touch, "oculus_touch");
	CHECK(same.sets["/actions/main"].sources.size() == 5);
	// One the table doesn't know: unchanged, with a warning
	BindingFile unknown = ConvertBindingFile(touch, "magic_wand");
	CHECK(unknown.sets["/actions/main"].sources.size() == 5);
	CHECK(unknown.warnings.size() == 1);
}
