#include "stdafx.h"

#include "LegacyBindings.h"

#include "Misc/SteamVRPaths.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

fs::path LegacyBindingsRelativePath(const std::string& controllerType)
{
	// The names in SteamVR's install
	if (controllerType == "knuckles")
		return "drivers/indexcontroller/resources/input/legacy_bindings_index_controller.json";
	if (controllerType == "oculus_touch")
		return "drivers/oculus/resources/input/legacy_bindings_touch.json";
	if (controllerType == "vive_controller")
		return "drivers/htc/resources/input/legacy_bindings_vive_controller.json";
	return "resources/config/legacy_bindings_generic.json";
}

namespace {

// One source of a built-in legacy file, for both hands: its input names' outputs are "<hand>_<output>"
struct LegacySource {
	const char* source; // under /user/hand/<hand>/input/
	const char* mode;
	std::vector<std::pair<const char*, const char*>> inputs; // input name -> output without the hand
	std::vector<std::pair<const char*, const char*>> parameters;
	const char* onlyHand = nullptr; // a source on one hand only (Touch's x and y, a and b)
};

std::vector<LegacySource> BuiltInSources(const std::string& controllerType)
{
	// Written by OpenComposite to map as SteamVR's legacy files do, for when SteamVR isn't installed
	if (controllerType == "knuckles") {
		return {
			{ "system", "button", { { "click", "system_press" } }, {} },
			{ "a", "button", { { "click", "grip_press" }, { "touch", "grip_touch" } }, {} },
			{ "b", "button", { { "click", "applicationmenu_press" }, { "touch", "applicationmenu_touch" } }, {} },
			{ "trigger", "trigger", { { "pull", "axis1_value" }, { "touch", "axis1_touch" } }, {} },
			{ "trigger", "button", { { "click", "axis1_press" }, { "touch", "axis1_touch" } },
			    { { "click_activate_threshold", "0.55" }, { "click_deactivate_threshold", "0.5" } } },
			{ "trackpad", "trackpad", { { "click", "axis0_press" }, { "position", "axis0_value" }, { "touch", "axis0_touch" } }, {} },
			{ "thumbstick", "joystick", { { "click", "axis0_press" }, { "position", "axis0_value" }, { "touch", "axis0_touch" } }, {} },
			{ "thumbstick", "button", { { "click", "axis0_press" } },
			    { { "force_input", "position" }, { "click_activate_threshold", "0.8" }, { "click_deactivate_threshold", "0.7" } } },
			{ "grip", "button", { { "click", "grip_press" }, { "touch", "grip_touch" } },
			    { { "force_input", "force" }, { "click_activate_threshold", "0.8" } } },
		};
	}
	if (controllerType == "oculus_touch") {
		return {
			{ "trigger", "trigger", { { "click", "axis1_press" }, { "pull", "axis1_value" }, { "touch", "axis1_touch" } }, {} },
			{ "joystick", "joystick", { { "click", "axis0_press" }, { "position", "axis0_value" }, { "touch", "axis0_touch" } }, {} },
			{ "grip", "trigger", { { "click", "axis2_press" }, { "pull", "axis2_value1" }, { "touch", "axis2_touch" } }, {} },
			{ "grip", "button", { { "click", "grip_press" }, { "touch", "grip_touch" } }, {} },
			{ "x", "button", { { "click", "a_press" }, { "touch", "a_touch" } }, {}, "left" },
			{ "y", "button", { { "click", "applicationmenu_press" }, { "touch", "applicationmenu_touch" } }, {}, "left" },
			{ "a", "button", { { "click", "a_press" }, { "touch", "a_touch" } }, {}, "right" },
			{ "b", "button", { { "click", "applicationmenu_press" }, { "touch", "applicationmenu_touch" } }, {}, "right" },
		};
	}
	if (controllerType == "vive_controller") {
		return {
			{ "system", "button", { { "click", "system_press" } }, {} },
			{ "grip", "button", { { "click", "grip_press" } }, {} },
			{ "application_menu", "button", { { "click", "applicationmenu_press" } }, {} },
			{ "trackpad", "trackpad", { { "click", "axis0_press" }, { "position", "axis0_value" }, { "touch", "axis0_touch" } }, {} },
			{ "trigger", "trigger", { { "pull", "axis1_value" }, { "touch", "axis1_touch" } }, {} },
			{ "trigger", "button", { { "click", "axis1_press" } }, {} },
		};
	}
	// generic
	return {
		{ "system", "button", { { "click", "system_press" } }, {} },
		{ "grip", "button", { { "click", "grip_press" } }, {} },
		{ "application_menu", "button", { { "click", "applicationmenu_press" } }, {} },
		{ "trackpad", "trackpad", { { "click", "axis0_press" }, { "position", "axis0_value" }, { "touch", "axis0_touch" } }, {} },
		{ "trigger", "trigger", { { "click", "axis1_press" }, { "pull", "axis1_value" }, { "touch", "axis1_touch" } }, {} },
		{ "joystick", "joystick", { { "position", "axis2_value2" } }, {} },
	};
}

bool IsFile(const fs::path& p)
{
	std::error_code ec;
	return !p.empty() && fs::is_regular_file(p, ec);
}

// "<hand>_<button>_<kind>": the button's id (or -1), its axis (or -1) and the kind
struct LegacyOutput {
	int button = -1;
	int axis = -1;
	std::string kind;
};
std::optional<LegacyOutput> ParseLegacyOutput(const std::string& actionPath, const std::string& hand)
{
	std::string prefix = std::string(kLegacyActionSet) + "/in/" + hand + "_";
	if (actionPath.size() <= prefix.size() || actionPath.compare(0, prefix.size(), prefix) != 0)
		return std::nullopt;
	std::string rest = actionPath.substr(prefix.size());
	size_t underscore = rest.rfind('_');
	if (underscore == std::string::npos)
		return std::nullopt;
	std::string button = rest.substr(0, underscore);
	LegacyOutput out;
	out.kind = rest.substr(underscore + 1);
	if (button == "system")
		out.button = vr::k_EButton_System;
	else if (button == "applicationmenu")
		out.button = vr::k_EButton_ApplicationMenu;
	else if (button == "grip")
		out.button = vr::k_EButton_Grip;
	else if (button == "a")
		out.button = vr::k_EButton_A;
	else if (button.size() == 5 && button.compare(0, 4, "axis") == 0 && button[4] >= '0' && button[4] <= '4') {
		out.axis = button[4] - '0';
		out.button = vr::k_EButton_Axis0 + out.axis;
	} else
		return std::nullopt;
	return out;
}

} // namespace

BindingFile BuiltInLegacyBindings(const std::string& controllerType)
{
	Json::Value root(Json::objectValue);
	root["controller_type"] = controllerType;
	Json::Value sources(Json::arrayValue);
	for (const char* hand : { "left", "right" }) {
		for (const LegacySource& s : BuiltInSources(controllerType)) {
			if (s.onlyHand && std::string(s.onlyHand) != hand)
				continue;
			Json::Value source(Json::objectValue);
			source["path"] = std::string("/user/hand/") + hand + "/input/" + s.source;
			source["mode"] = s.mode;
			Json::Value inputs(Json::objectValue);
			for (const auto& [name, output] : s.inputs)
				inputs[name]["output"] = std::string(kLegacyActionSet) + "/in/" + hand + "_" + output;
			source["inputs"] = inputs;
			Json::Value parameters(Json::objectValue);
			for (const auto& [name, value] : s.parameters)
				parameters[name] = value;
			source["parameters"] = parameters;
			sources.append(source);
		}
	}
	Json::Value set(Json::objectValue);
	set["sources"] = sources;
	Json::Value haptics(Json::arrayValue);
	for (const char* hand : { "left", "right" }) {
		Json::Value h(Json::objectValue);
		h["output"] = std::string("/actions/legacy/out/") + hand + "_haptic";
		h["path"] = std::string("/user/hand/") + hand + "/output/haptic";
		haptics.append(h);
	}
	set["haptics"] = haptics;
	root["bindings"][kLegacyActionSet] = set;
	return ParseBindingFile(Json::writeString(Json::StreamWriterBuilder(), root));
}

BindingFile LoadLegacyBindingsFrom(const std::string& controllerType, const std::optional<fs::path>& steamvrDir)
{
	if (steamvrDir) {
		fs::path file = *steamvrDir / LegacyBindingsRelativePath(controllerType);
		if (IsFile(file)) {
			BindingFile loaded = LoadBindingFile(file);
			if (loaded.sets.count(kLegacyActionSet))
				return loaded;
		}
	}
	return BuiltInLegacyBindings(controllerType);
}

const BindingFile* LoadLegacyBindings(const std::string& controllerType)
{
	static std::mutex lock;
	static std::map<std::string, BindingFile> cache;
	std::lock_guard<std::mutex> guard(lock);
	auto it = cache.find(controllerType);
	if (it == cache.end())
		it = cache.emplace(controllerType, LoadLegacyBindingsFrom(controllerType, FindSteamVRInstall())).first;
	return &it->second;
}

void ApplyLegacyOutputs(const ActionOutputs& out, const std::string& hand, vr::VRControllerState_t& state)
{
	for (const auto& [action, d] : out.digital) {
		std::optional<LegacyOutput> o = ParseLegacyOutput(action, hand);
		if (!o || !d.state)
			continue;
		if (o->kind == "press")
			state.ulButtonPressed |= (1ull << o->button);
		else if (o->kind == "touch")
			state.ulButtonTouched |= (1ull << o->button);
	}
	for (const auto& [action, a] : out.analog) {
		std::optional<LegacyOutput> o = ParseLegacyOutput(action, hand);
		if (!o || o->axis < 0)
			continue;
		// value: what the mode gives (x, or x and y); value1: x only; value2: x and y
		vr::VRControllerAxis_t& axis = state.rAxis[o->axis];
		if (o->kind == "value" || o->kind == "value2") {
			axis.x = a.x;
			axis.y = a.y;
		} else if (o->kind == "value1") {
			axis.x = a.x;
			axis.y = 0;
		}
	}
}

uint64_t LegacySupportedButtons(const BindingFile& file, const std::string& hand)
{
	uint64_t buttons = 0;
	auto set = file.sets.find(kLegacyActionSet);
	if (set == file.sets.end())
		return 0;
	for (const SourceBinding& source : set->second.sources) {
		for (const auto& [input, output] : source.inputs) {
			std::optional<LegacyOutput> o = ParseLegacyOutput(output, hand);
			if (o && (o->kind == "press" || o->kind == "touch"))
				buttons |= (1ull << o->button);
		}
	}
	return buttons;
}

vr::EVRControllerAxisType LegacyAxisType(const BindingFile& file, const std::string& hand, int axis)
{
	auto set = file.sets.find(kLegacyActionSet);
	if (set == file.sets.end())
		return vr::k_eControllerAxis_None;
	for (const SourceBinding& source : set->second.sources) {
		for (const auto& [input, output] : source.inputs) {
			std::optional<LegacyOutput> o = ParseLegacyOutput(output, hand);
			if (!o || o->axis != axis || o->kind.compare(0, 5, "value") != 0)
				continue;
			if (source.mode == "trackpad")
				return vr::k_eControllerAxis_TrackPad;
			if (source.mode == "joystick")
				return vr::k_eControllerAxis_Joystick;
			return vr::k_eControllerAxis_Trigger;
		}
	}
	return vr::k_eControllerAxis_None;
}
