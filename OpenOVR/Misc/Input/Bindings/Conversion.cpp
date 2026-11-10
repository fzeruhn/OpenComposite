#include "stdafx.h"

#include "Conversion.h"

#include <map>
#include <vector>

namespace {

// The part an input plays on its controller
enum class Role { Trigger, Grip, Primary, Secondary, Menu, System, Pad, Stick };

struct ControllerInputs {
	// Each role's input name on the left and right hand ("" for none)
	std::map<Role, std::pair<std::string, std::string>> inputs;
};

// kConversions: every controller type of the fork's profiles (their GetOpenVRName()), and generic. Index's and
// Touch's face buttons pair by hand and position: Touch's left x and y and right a and b are Index's a and b on each
// hand, the lower one primary. An estimate, not measured against SteamVR.
const std::map<std::string, ControllerInputs>& kConversions()
{
	static const std::map<std::string, ControllerInputs> table = {
		{ "knuckles",
		    { { { Role::Trigger, { "trigger", "trigger" } }, { Role::Grip, { "grip", "grip" } }, { Role::Primary, { "a", "a" } },
		        { Role::Secondary, { "b", "b" } }, { Role::System, { "system", "system" } }, { Role::Pad, { "trackpad", "trackpad" } },
		        { Role::Stick, { "thumbstick", "thumbstick" } } } } },
		{ "oculus_touch",
		    { { { Role::Trigger, { "trigger", "trigger" } }, { Role::Grip, { "grip", "grip" } }, { Role::Primary, { "x", "a" } },
		        { Role::Secondary, { "y", "b" } }, { Role::Menu, { "application_menu", "" } }, { Role::System, { "", "system" } },
		        { Role::Stick, { "joystick", "joystick" } } } } },
		{ "hpmotioncontroller",
		    { { { Role::Trigger, { "trigger", "trigger" } }, { Role::Grip, { "grip", "grip" } }, { Role::Primary, { "x", "a" } },
		        { Role::Secondary, { "y", "b" } }, { Role::Menu, { "application_menu", "application_menu" } },
		        { Role::Stick, { "joystick", "joystick" } } } } },
		{ "vive_controller",
		    { { { Role::Trigger, { "trigger", "trigger" } }, { Role::Grip, { "grip", "grip" } },
		        { Role::Menu, { "application_menu", "application_menu" } }, { Role::System, { "system", "system" } },
		        { Role::Pad, { "trackpad", "trackpad" } } } } },
		{ "holographic_controller",
		    { { { Role::Trigger, { "trigger", "trigger" } }, { Role::Grip, { "grip", "grip" } },
		        { Role::Menu, { "application_menu", "application_menu" } }, { Role::System, { "system", "system" } },
		        { Role::Pad, { "trackpad", "trackpad" } }, { Role::Stick, { "joystick", "joystick" } } } } },
		{ "khr_simple_controller",
		    { { { Role::Trigger, { "trigger", "trigger" } }, { Role::Menu, { "application_menu", "application_menu" } } } } },
		{ "generic",
		    { { { Role::Trigger, { "trigger", "trigger" } }, { Role::Grip, { "grip", "grip" } },
		        { Role::Menu, { "application_menu", "application_menu" } }, { Role::System, { "system", "system" } },
		        { Role::Pad, { "trackpad", "trackpad" } } } } },
	};
	return table;
}

// What a role falls back to on a controller that hasn't it
std::vector<Role> Fallbacks(Role r)
{
	switch (r) {
	case Role::Pad:
		return { Role::Stick };
	case Role::Stick:
		return { Role::Pad };
	case Role::Menu:
		return { Role::System };
	case Role::System:
		return { Role::Menu };
	default:
		return {};
	}
}

struct Split {
	std::string prefix; // /user/hand/left/input/
	bool right = false;
	std::string input; // x
	std::string rest; // anything after the input (/click)
};

std::optional<Split> SplitPath(const std::string& path)
{
	for (const char* hand : { "/user/hand/left/input/", "/user/hand/right/input/" }) {
		std::string p = hand;
		if (path.compare(0, p.size(), p) != 0)
			continue;
		Split s;
		s.prefix = p;
		s.right = p.find("right") != std::string::npos;
		std::string tail = path.substr(p.size());
		size_t slash = tail.find('/');
		s.input = tail.substr(0, slash);
		s.rest = slash == std::string::npos ? std::string() : tail.substr(slash);
		return s;
	}
	return std::nullopt;
}

} // namespace

std::optional<std::string> ConvertSourcePath(const std::string& path, const std::string& fromType, const std::string& toType)
{
	auto from = kConversions().find(fromType), to = kConversions().find(toType);
	if (from == kConversions().end() || to == kConversions().end())
		return std::nullopt;
	std::optional<Split> s = SplitPath(path);
	if (!s)
		return std::nullopt;

	std::optional<Role> role;
	for (const auto& [r, names] : from->second.inputs) {
		const std::string& name = s->right ? names.second : names.first;
		if (!name.empty() && name == s->input)
			role = r;
	}
	if (!role)
		return std::nullopt;

	std::vector<Role> tries = { *role };
	for (Role f : Fallbacks(*role))
		tries.push_back(f);
	// A controller whose one 2D input is its pad (Vive, generic) moves to the other's stick when it has one: the
	// controller's main 2D input, as Index's thumbstick is and its small force pad isn't
	if (*role == Role::Pad && !from->second.inputs.count(Role::Stick) && to->second.inputs.count(Role::Stick))
		tries = { Role::Stick, Role::Pad };
	for (Role r : tries) {
		auto it = to->second.inputs.find(r);
		if (it == to->second.inputs.end())
			continue;
		const std::string& name = s->right ? it->second.second : it->second.first;
		if (!name.empty())
			return s->prefix + name + s->rest;
	}
	return std::nullopt;
}

BindingFile ConvertBindingFile(const BindingFile& from, const std::string& toControllerType)
{
	BindingFile to = from;
	if (from.controllerType == toControllerType)
		return to;
	to.controllerType = toControllerType;
	bool known = kConversions().count(from.controllerType) && kConversions().count(toControllerType);
	if (!known) {
		to.warnings.push_back("no conversion from " + from.controllerType + " to " + toControllerType + ": the bindings are used as they are");
		return to;
	}

	for (auto& [setName, set] : to.sets) {
		std::vector<SourceBinding> sources;
		for (SourceBinding s : set.sources) {
			std::optional<std::string> path = ConvertSourcePath(s.path, from.controllerType, toControllerType);
			if (!path) {
				to.warnings.push_back(setName + ": " + s.path + " has no counterpart on " + toControllerType + ", dropped");
				continue;
			}
			// A pad's source on a stick, or the other way: the same mode, but a trackpad mode becomes a joystick one
			bool toStick = path->find("thumbstick") != std::string::npos || path->find("joystick") != std::string::npos;
			bool toPad = path->find("trackpad") != std::string::npos;
			if (toStick && s.mode == "trackpad")
				s.mode = "joystick";
			else if (toPad && s.mode == "joystick")
				s.mode = "trackpad";
			s.path = *path;
			sources.push_back(std::move(s));
		}
		set.sources = std::move(sources);

		std::vector<ChordBinding> chords;
		for (ChordBinding c : set.chords) {
			bool ok = true;
			for (auto& [path, input] : c.inputs) {
				std::optional<std::string> p = ConvertSourcePath(path, from.controllerType, toControllerType);
				if (!p) {
					ok = false;
					break;
				}
				path = *p;
			}
			if (ok)
				chords.push_back(std::move(c));
			else
				to.warnings.push_back(setName + ": a chord on " + c.output + " has an input with no counterpart on " + toControllerType + ", dropped");
		}
		set.chords = std::move(chords);
	}
	return to;
}
