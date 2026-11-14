#include "stdafx.h"

#include "Modes.h"
#include "Misc/Numbers.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <map>

// Valve doesn't document the modes' parameters (the openvr wiki covers the action manifest, not binding modes); their
// names are those SteamVR's own binding files use. A default comes from what OpenComposite's input already did, or is
// an estimate not yet measured against SteamVR.
const std::vector<ModeParameter> kModeParameters = {
	// button and complex_button
	{ "button", "click_activate_threshold", 0.7f, "today's BaseInput (perProfileData's default)" },
	{ "button", "click_deactivate_threshold", 0.55f, "today's BaseInput (perProfileData's default)" },
	{ "button", "double_press_delay", 0.5f, "today's BaseInput (500 ms between the presses, from OpenOVR merge request !186)" },
	{ "button", "long_press_delay", 0.5f, "an estimate, not measured against SteamVR" },
	{ "complex_button", "click_activate_threshold", 0.7f, "today's BaseInput (perProfileData's default)" },
	{ "complex_button", "click_deactivate_threshold", 0.55f, "today's BaseInput (perProfileData's default)" },
	{ "complex_button", "double_press_delay", 0.5f, "today's BaseInput (500 ms between the presses, from OpenOVR merge request !186)" },
	{ "complex_button", "long_press_delay", 0.5f, "an estimate, not measured against SteamVR" },
	// toggle_button
	{ "toggle_button", "click_activate_threshold", 0.7f, "today's BaseInput (perProfileData's default)" },
	{ "toggle_button", "click_deactivate_threshold", 0.55f, "today's BaseInput (perProfileData's default)" },
	// trigger: click from the pull, as today's input does for every trigger
	{ "trigger", "click_activate_threshold", 0.7f, "today's BaseInput (perProfileData's default)" },
	{ "trigger", "click_deactivate_threshold", 0.55f, "today's BaseInput (perProfileData's default)" },
	{ "trigger", "touch_threshold", 0.01f, "an estimate, not measured against SteamVR: touched as soon as it moves" },
	// grab
	{ "grab", "value_hold_threshold", 0.7f, "today's BaseInput (SteamVR's defaults, BaseInput.cpp)" },
	{ "grab", "value_release_threshold", 0.65f, "today's BaseInput (SteamVR's defaults, BaseInput.cpp)" },
	{ "grab", "force_hold_threshold", 0.6f, "an estimate, not measured against SteamVR" },
	{ "grab", "force_release_threshold", 0.5f, "an estimate, not measured against SteamVR" },
	// scalar_constant
	{ "scalar_constant", "value", 1.0f, "an estimate, not measured against SteamVR" },
	// trackpad and joystick: position, click, touch
	{ "trackpad", "deadzone_pct", 0.0f, "today's BaseInput (perProfileData's default)" },
	{ "joystick", "deadzone_pct", 0.0f, "today's BaseInput (perProfileData's default)" },
	{ "joystick", "maxzone_pct", 100.0f, "today's BaseInput (perProfileData's default)" },
	// dpad (the click and touch sub-modes of OpenOVR merge request !186)
	{ "dpad", "deadzone_pct", 50.0f, "today's BaseInput (DpadBindingInfo::dpadDeadzoneRadius)" },
	{ "dpad", "overlap_pct", 0.0f, "today's BaseInput (four 90 degree sectors)" },
	// scroll
	{ "scroll", "discrete_scroll_step", 0.2f, "an estimate, not measured against SteamVR: a fifth of the pad per step" },
	{ "scroll", "smooth_scroll_multiplier", 1.0f, "an estimate, not measured against SteamVR" },
	// radial
	{ "radial", "segments", 4.0f, "an estimate, not measured against SteamVR" },
	{ "radial", "deadzone_pct", 25.0f, "an estimate, not measured against SteamVR" },
};

float ModeParameterDefault(const std::string& mode, const std::string& name)
{
	for (const ModeParameter& p : kModeParameters)
		if (mode == p.mode && name == p.name)
			return p.defaultValue;
	return 0;
}

std::optional<float> RawInputState::Get(const std::string& path) const
{
	auto it = value.find(path);
	if (it == value.end())
		return std::nullopt;
	return it->second;
}

namespace {

// A click from a force sensor, for a source that has a force and no click (Index's trackpad): on at this force, off
// below the second. An estimate, not measured against SteamVR.
constexpr float kForceClickOn = 0.3f, kForceClickOff = 0.25f;

// What every mode shares: its source, its parameters, and its inputs' last states (for change times and chords)
class ModeBase : public ModeBinding {
public:
	explicit ModeBase(const SourceBinding& binding)
	    : binding(binding)
	{
	}

	std::optional<bool> DigitalInput(const std::string& inputName) const override
	{
		auto it = states.find(inputName);
		if (it == states.end())
			return std::nullopt;
		return it->second.state;
	}

protected:
	float Param(const std::string& name) const
	{
		auto it = binding.parameters.find(name);
		if (it != binding.parameters.end()) {
			if (std::optional<float> v = Numbers::ParseFloat(it->second))
				return *v;
		}
		return ModeParameterDefault(binding.mode, name);
	}
	std::string TextParam(const std::string& name, const std::string& fallback) const
	{
		auto it = binding.parameters.find(name);
		return it == binding.parameters.end() ? fallback : it->second;
	}
	std::string Raw(const std::string& component) const { return binding.path + "/" + component; }

	// Records an input's state this frame, and writes it to its action if it's bound to one
	void Digital(ActionOutputs& out, const std::string& input, bool state, double time)
	{
		DigitalOut& s = states[input];
		s.changed = s.state != state;
		if (s.changed)
			s.changeTime = time;
		s.state = state;
		s.origin = binding.path;
		auto bound = binding.inputs.find(input);
		if (bound != binding.inputs.end())
			out.digital[bound->second] = s;
	}
	void Analog(ActionOutputs& out, const std::string& input, float x, float y, float z)
	{
		AnalogOut& a = analogs[input];
		a.dx = x - a.x;
		a.dy = y - a.y;
		a.dz = z - a.z;
		a.x = x;
		a.y = y;
		a.z = z;
		a.origin = binding.path;
		auto bound = binding.inputs.find(input);
		if (bound != binding.inputs.end())
			out.analog[bound->second] = a;
	}

	// A threshold crossed one way to turn on and another to turn off
	static bool Hysteresis(bool was, float v, float on, float off) { return was ? v > off : v >= on; }

	SourceBinding binding;
	bool forceClick = false; // a click from the force (kForceClickOn), for sources without a click
	std::map<std::string, DigitalOut> states;
	std::map<std::string, AnalogOut> analogs;
};

// button (and complex_button): click, touch, double (a second press soon after the first, held until released, counted
// once), long (held for long_press_delay) and held (as long)
class ButtonMode : public ModeBase {
public:
	using ModeBase::ModeBase;

	std::vector<std::string> RawInputs() const override
	{
		if (TextParam("force_input", "click") == "position")
			return { Raw("x"), Raw("y"), Raw("touch") };
		return { Raw("click"), Raw("value"), Raw("force"), Raw("touch") };
	}

	void Update(const RawInputState& in, ActionOutputs& out) override
	{
		bool pressed = Pressed(in);
		std::optional<float> touch = in.Get(Raw("touch"));
		Digital(out, "click", pressed, in.time);
		Digital(out, "touch", touch ? *touch >= 0.5f || pressed : pressed, in.time);

		// The double press: a press within double_press_delay of the last one, true until released
		if (pressed && !wasPressed) {
			if (lastPressTime >= 0 && in.time - lastPressTime <= Param("double_press_delay")) {
				doubled = true;
				lastPressTime = -1; // the third press starts again
			} else {
				lastPressTime = in.time;
			}
			pressStart = in.time;
		}
		if (!pressed)
			doubled = false;
		Digital(out, "double", doubled, in.time);

		bool longPress = pressed && in.time - pressStart >= Param("long_press_delay");
		Digital(out, "long", longPress, in.time);
		Digital(out, "held", longPress, in.time);
		wasPressed = pressed;
	}

private:
	bool Pressed(const RawInputState& in)
	{
		// force_input picks what clicks it: the click (default), the value or the force through the thresholds, or the
		// position: how far a stick or pad is pushed, through the thresholds (SteamVR's legacy file for Index presses
		// axis 0 so, at 0.8 and 0.7)
		std::string force = TextParam("force_input", "click");
		if (force == "position") {
			float x = in.Get(Raw("x")).value_or(0), y = in.Get(Raw("y")).value_or(0);
			state = Hysteresis(state, std::sqrt(x * x + y * y), Param("click_activate_threshold"), Param("click_deactivate_threshold"));
			return state;
		}
		std::optional<float> click = in.Get(Raw("click"));
		std::optional<float> analog = force == "force" ? in.Get(Raw("force")) : in.Get(Raw("value"));
		if (force == "click" && click)
			state = *click >= 0.5f;
		else if (analog)
			state = Hysteresis(state, *analog, Param("click_activate_threshold"), Param("click_deactivate_threshold"));
		else if (click)
			state = *click >= 0.5f;
		else if (std::optional<float> f = in.Get(Raw("force")))
			state = Hysteresis(state, *f, kForceClickOn, kForceClickOff); // no click: the force (Index's trackpad)
		else
			state = false;
		return state;
	}

	bool state = false, wasPressed = false, doubled = false;
	double lastPressTime = -1, pressStart = 0;
};

// toggle_button: its click flips on each press
class ToggleMode : public ModeBase {
public:
	using ModeBase::ModeBase;

	std::vector<std::string> RawInputs() const override { return { Raw("click"), Raw("value"), Raw("force") }; }

	void Update(const RawInputState& in, ActionOutputs& out) override
	{
		std::optional<float> click = in.Get(Raw("click"));
		std::optional<float> value = in.Get(Raw("value"));
		std::optional<float> force = in.Get(Raw("force"));
		if (click)
			pressed = *click >= 0.5f;
		else if (value)
			pressed = Hysteresis(pressed, *value, Param("click_activate_threshold"), Param("click_deactivate_threshold"));
		else if (force)
			pressed = Hysteresis(pressed, *force, kForceClickOn, kForceClickOff); // no click: the force (Index's trackpad)
		else
			pressed = false;
		if (pressed && !wasPressed)
			on = !on;
		wasPressed = pressed;
		Digital(out, "click", on, in.time);
	}

private:
	bool pressed = false, wasPressed = false, on = false;
};

// trigger: pull (the value), click (the value through the thresholds, else the raw click) and touch
class TriggerMode : public ModeBase {
public:
	using ModeBase::ModeBase;

	std::vector<std::string> RawInputs() const override { return { Raw("value"), Raw("click"), Raw("touch") }; }

	void Update(const RawInputState& in, ActionOutputs& out) override
	{
		std::optional<float> value = in.Get(Raw("value"));
		std::optional<float> click = in.Get(Raw("click"));
		std::optional<float> touch = in.Get(Raw("touch"));
		float pull = value ? *value : (click ? *click : 0.0f);
		Analog(out, "pull", pull, 0, 0);
		if (value)
			clicked = Hysteresis(clicked, *value, Param("click_activate_threshold"), Param("click_deactivate_threshold"));
		else
			clicked = click && *click >= 0.5f;
		Digital(out, "click", clicked, in.time);
		Digital(out, "touch", touch ? *touch >= 0.5f || clicked : pull >= Param("touch_threshold"), in.time);
	}

private:
	bool clicked = false;
};

// grab: held once the value or the force passes its hold threshold, released once both are under their release ones;
// a grip with only a click (converted to a controller without a squeeze value) follows the click
class GrabMode : public ModeBase {
public:
	using ModeBase::ModeBase;

	std::vector<std::string> RawInputs() const override { return { Raw("value"), Raw("force"), Raw("click") }; }

	void Update(const RawInputState& in, ActionOutputs& out) override
	{
		std::optional<float> rawValue = in.Get(Raw("value"));
		std::optional<float> force = in.Get(Raw("force"));
		std::optional<float> click = in.Get(Raw("click"));
		float value = rawValue.value_or(0);
		if (!rawValue && !force && click)
			grabbed = *click >= 0.5f;
		else if (!grabbed)
			grabbed = value >= Param("value_hold_threshold") || (force && *force >= Param("force_hold_threshold"));
		else
			grabbed = value > Param("value_release_threshold") || (force && *force > Param("force_release_threshold"));
		Digital(out, "grab", grabbed, in.time);
		Digital(out, "held", grabbed, in.time);
	}

private:
	bool grabbed = false;
};

// scalar_constant: its value parameter, always
class ScalarConstantMode : public ModeBase {
public:
	using ModeBase::ModeBase;

	std::vector<std::string> RawInputs() const override { return {}; }

	void Update(const RawInputState& in, ActionOutputs& out) override { Analog(out, "value", Param("value"), 0, 0); }
};

// force_sensor: the force (the value where there's none)
class ForceSensorMode : public ModeBase {
public:
	using ModeBase::ModeBase;

	std::vector<std::string> RawInputs() const override { return { Raw("force"), Raw("value") }; }

	void Update(const RawInputState& in, ActionOutputs& out) override
	{
		std::optional<float> force = in.Get(Raw("force"));
		Analog(out, "force", force ? *force : in.Get(Raw("value")).value_or(0), 0, 0);
	}
};

// The position of a trackpad or thumbstick, and whether it's touched and clicked (a thumbstick without a touch sensor
// is touched while it's off centre)
struct Stick {
	float x = 0, y = 0;
	bool touched = false, clicked = false;
	float Radius() const { return std::sqrt(x * x + y * y); }
};
Stick ReadStick(const RawInputState& in, const std::string& path, bool& forceClick)
{
	Stick s;
	s.x = in.Get(path + "/x").value_or(0);
	s.y = in.Get(path + "/y").value_or(0);
	std::optional<float> click = in.Get(path + "/click"), force = in.Get(path + "/force");
	if (click)
		s.clicked = *click >= 0.5f;
	else if (force) // a pad with a force sensor and no click (Index's)
		s.clicked = forceClick = forceClick ? *force > kForceClickOff : *force >= kForceClickOn;
	std::optional<float> touch = in.Get(path + "/touch");
	s.touched = touch ? *touch >= 0.5f || s.clicked : s.Radius() > 0.0f || s.clicked;
	return s;
}

// trackpad: position (zero inside the deadzone), touch and click
class TrackpadMode : public ModeBase {
public:
	using ModeBase::ModeBase;

	std::vector<std::string> RawInputs() const override { return { Raw("x"), Raw("y"), Raw("touch"), Raw("click"), Raw("force") }; }

	void Update(const RawInputState& in, ActionOutputs& out) override
	{
		Stick s = ReadStick(in, binding.path, forceClick);
		bool inside = s.Radius() < Param("deadzone_pct") / 100.0f;
		Analog(out, "position", inside ? 0 : s.x, inside ? 0 : s.y, 0);
		Digital(out, "touch", s.touched, in.time);
		Digital(out, "click", s.clicked, in.time);
	}
};

// joystick: position scaled radially from the deadzone (zero) to the maxzone (full), click and touch
class JoystickMode : public ModeBase {
public:
	using ModeBase::ModeBase;

	std::vector<std::string> RawInputs() const override { return { Raw("x"), Raw("y"), Raw("touch"), Raw("click"), Raw("force") }; }

	void Update(const RawInputState& in, ActionOutputs& out) override
	{
		Stick s = ReadStick(in, binding.path, forceClick);
		float r = s.Radius();
		float dead = Param("deadzone_pct") / 100.0f, max = Param("maxzone_pct") / 100.0f;
		float x = 0, y = 0;
		if (r > dead && r > 0) {
			float scaled = max > dead ? std::min(1.0f, (r - dead) / (max - dead)) : 1.0f;
			x = s.x / r * scaled;
			y = s.y / r * scaled;
		}
		Analog(out, "position", x, y, 0);
		Digital(out, "click", s.clicked, in.time);
		Digital(out, "touch", s.touched, in.time);
	}
};

// dpad: north, east, south, west outside the deadzone (sectors of 90 degrees, widened by overlap_pct so diagonals give
// two) and center inside it; with sub_mode click only while clicked, else while touched
class DpadMode : public ModeBase {
public:
	using ModeBase::ModeBase;

	std::vector<std::string> RawInputs() const override { return { Raw("x"), Raw("y"), Raw("touch"), Raw("click"), Raw("force") }; }

	void Update(const RawInputState& in, ActionOutputs& out) override
	{
		Stick s = ReadStick(in, binding.path, forceClick);
		bool on = TextParam("sub_mode", "touch") == "click" ? s.clicked : s.touched;
		float r = s.Radius();
		bool centre = r <= Param("deadzone_pct") / 100.0f;
		float angle = std::atan2(s.y, s.x) * 180.0f / 3.14159265f; // east 0, north 90
		float half = 45.0f * (1.0f + Param("overlap_pct") / 100.0f);
		auto inSector = [&](float direction) {
			float d = std::fabs(std::remainder(angle - direction, 360.0f));
			return on && !centre && d <= half;
		};
		Digital(out, "north", inSector(90), in.time);
		Digital(out, "east", inSector(0), in.time);
		Digital(out, "south", inSector(-90), in.time);
		Digital(out, "west", inSector(180), in.time);
		Digital(out, "center", on && centre, in.time);
	}
};

// scroll: a finger dragged on the pad. smooth: the movement since the last frame (times smooth_scroll_multiplier);
// discrete (the default): one step (+1 or -1) each time it has moved discrete_scroll_step
class ScrollMode : public ModeBase {
public:
	using ModeBase::ModeBase;

	std::vector<std::string> RawInputs() const override { return { Raw("x"), Raw("y"), Raw("touch") }; }

	void Update(const RawInputState& in, ActionOutputs& out) override
	{
		Stick s = ReadStick(in, binding.path, forceClick);
		float dx = 0, dy = 0;
		if (s.touched && wasTouched) {
			dx = s.x - lastX;
			dy = s.y - lastY;
		}
		wasTouched = s.touched;
		lastX = s.x;
		lastY = s.y;
		if (!s.touched) {
			pendingX = pendingY = 0;
			Analog(out, "scroll", 0, 0, 0);
			return;
		}
		if (TextParam("scroll_mode", "discrete") == "smooth") {
			float k = Param("smooth_scroll_multiplier");
			Analog(out, "scroll", dx * k, dy * k, 0);
			return;
		}
		// A step's worth less a hair, so tenths of a pad add up to a fifth
		float step = Param("discrete_scroll_step") - 1e-5f;
		pendingX += dx;
		pendingY += dy;
		float sx = 0, sy = 0;
		if (step > 1e-4f) {
			while (pendingY >= step) {
				sy += 1;
				pendingY -= step;
			}
			while (pendingY <= -step) {
				sy -= 1;
				pendingY += step;
			}
			while (pendingX >= step) {
				sx += 1;
				pendingX -= step;
			}
			while (pendingX <= -step) {
				sx -= 1;
				pendingX += step;
			}
		}
		Analog(out, "scroll", sx, sy, 0);
	}

private:
	bool wasTouched = false;
	float lastX = 0, lastY = 0, pendingX = 0, pendingY = 0;
};

// radial: the pad cut into segments (the first centred on north, then clockwise); the one under the finger is on, as
// with dpad's sub_mode, outside the deadzone. Inputs "0", "1"... as the segments' numbers.
class RadialMode : public ModeBase {
public:
	using ModeBase::ModeBase;

	std::vector<std::string> RawInputs() const override { return { Raw("x"), Raw("y"), Raw("touch"), Raw("click"), Raw("force") }; }

	void Update(const RawInputState& in, ActionOutputs& out) override
	{
		Stick s = ReadStick(in, binding.path, forceClick);
		bool on = TextParam("sub_mode", "touch") == "click" ? s.clicked : s.touched;
		float wanted = Param("segments");
		// 1 to 360 (a hand-edited count can't make a frame loop run forever)
		int segments = std::isfinite(wanted) ? (int)std::clamp(wanted, 1.0f, 360.0f) : 4;
		int under = -1;
		if (on && s.Radius() > Param("deadzone_pct") / 100.0f) {
			float clockwiseFromNorth = 90.0f - std::atan2(s.y, s.x) * 180.0f / 3.14159265f;
			float width = 360.0f / (float)segments;
			float a = std::fmod(clockwiseFromNorth + width / 2 + 720.0f, 360.0f);
			under = std::min(segments - 1, (int)(a / width));
		}
		for (int i = 0; i < segments; i++)
			Digital(out, std::to_string(i), i == under, in.time);
	}
};

using Factory = std::function<std::unique_ptr<ModeBinding>(const SourceBinding&)>;

const std::map<std::string, Factory>& Factories()
{
	static const std::map<std::string, Factory> factories = {
		{ "button", [](const SourceBinding& b) { return std::make_unique<ButtonMode>(b); } },
		{ "complex_button", [](const SourceBinding& b) { return std::make_unique<ButtonMode>(b); } },
		{ "toggle_button", [](const SourceBinding& b) { return std::make_unique<ToggleMode>(b); } },
		{ "trigger", [](const SourceBinding& b) { return std::make_unique<TriggerMode>(b); } },
		{ "grab", [](const SourceBinding& b) { return std::make_unique<GrabMode>(b); } },
		{ "scalar_constant", [](const SourceBinding& b) { return std::make_unique<ScalarConstantMode>(b); } },
		{ "force_sensor", [](const SourceBinding& b) { return std::make_unique<ForceSensorMode>(b); } },
		{ "trackpad", [](const SourceBinding& b) { return std::make_unique<TrackpadMode>(b); } },
		{ "joystick", [](const SourceBinding& b) { return std::make_unique<JoystickMode>(b); } },
		{ "dpad", [](const SourceBinding& b) { return std::make_unique<DpadMode>(b); } },
		{ "scroll", [](const SourceBinding& b) { return std::make_unique<ScrollMode>(b); } },
		{ "radial", [](const SourceBinding& b) { return std::make_unique<RadialMode>(b); } },
	};
	return factories;
}

} // namespace

std::unique_ptr<ModeBinding> CreateModeBinding(const SourceBinding& binding, std::vector<std::string>& warnings)
{
	auto it = Factories().find(binding.mode);
	if (it == Factories().end()) {
		warnings.push_back(binding.path + ": mode '" + binding.mode + "' isn't one OpenComposite evaluates, skipped");
		return nullptr;
	}
	return it->second(binding);
}
