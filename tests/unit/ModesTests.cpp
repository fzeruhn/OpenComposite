// doctest first: stdafx.h brings in Xlib on Linux, whose macros (None, Bool, ...) clash with doctest's names
#include "doctest.h"

#include "stdafx.h"

#include "Misc/Input/Bindings/Modes.h"

#include <cmath>

namespace {
const std::string kSrc = "/user/hand/right/input/trigger";

SourceBinding Source(const std::string& mode, std::map<std::string, std::string> inputs, std::map<std::string, std::string> parameters = {},
    const std::string& path = kSrc)
{
	SourceBinding b;
	b.path = path;
	b.mode = mode;
	b.inputs = std::move(inputs);
	b.parameters = std::move(parameters);
	return b;
}

// Runs a binding a frame at a time (60 Hz) and answers an action's outputs
struct Runner {
	std::unique_ptr<ModeBinding> mode;
	ActionOutputs out;
	RawInputState in;
	int frame = 0;
	explicit Runner(const SourceBinding& b)
	{
		std::vector<std::string> warnings;
		mode = CreateModeBinding(b, warnings);
		REQUIRE(mode);
		CHECK(warnings.empty());
	}
	// Doubles, so the tests' whole numbers (0, 1) need no conversion warning
	void Step(std::map<std::string, double> raw, double seconds = 1.0 / 60)
	{
		in.time = frame++ * seconds;
		in.value.clear();
		for (auto& [k, v] : raw)
			in.value[k] = static_cast<float>(v);
		mode->Update(in, out);
	}
	bool Digital(const std::string& action) { return out.digital[action].state; }
	float Analog(const std::string& action) { return out.analog[action].x; }
};
} // namespace

TEST_CASE("Button_click_follows_raw_click")
{
	Runner r(Source("button", { { "click", "/a/click" }, { "touch", "/a/touch" } }, {}, "/user/hand/right/input/a"));
	r.Step({ { "/user/hand/right/input/a/click", 0 }, { "/user/hand/right/input/a/touch", 1 } });
	CHECK_FALSE(r.Digital("/a/click"));
	CHECK(r.Digital("/a/touch"));
	r.Step({ { "/user/hand/right/input/a/click", 1 } });
	CHECK(r.Digital("/a/click"));
	CHECK(r.out.digital["/a/click"].changed);
	CHECK(r.out.digital["/a/click"].origin == "/user/hand/right/input/a");
	CHECK(r.Digital("/a/touch")); // pressed is touched
	r.Step({ { "/user/hand/right/input/a/click", 1 } });
	CHECK_FALSE(r.out.digital["/a/click"].changed);
	r.Step({ { "/user/hand/right/input/a/click", 0 } });
	CHECK_FALSE(r.Digital("/a/click"));
	CHECK(r.mode->DigitalInput("click") == false);
	CHECK_FALSE(r.mode->DigitalInput("north").has_value());
}

TEST_CASE("Button_double_press_counted_once")
{
	const std::string a = "/user/hand/right/input/a/click";
	Runner r(Source("button", { { "double", "/a/double" } }, {}, "/user/hand/right/input/a"));
	int rises = 0;
	auto step = [&](float v) {
		bool before = r.Digital("/a/double");
		r.Step({ { a, v } });
		rises += !before && r.Digital("/a/double");
	};
	step(1); // first press
	step(0);
	step(1); // second, 2 frames later: double, held while pressed
	CHECK(r.Digital("/a/double"));
	step(1);
	step(1);
	step(0);
	CHECK_FALSE(r.Digital("/a/double"));
	step(1); // a third press starts again
	CHECK_FALSE(r.Digital("/a/double"));
	CHECK(rises == 1);

	// Too slow: no double
	Runner slow(Source("button", { { "double", "/a/double" } }, {}, "/user/hand/right/input/a"));
	slow.Step({ { a, 1 } }, 0.4);
	slow.Step({ { a, 0 } }, 0.4);
	slow.Step({ { a, 1 } }, 0.4);
	CHECK_FALSE(slow.Digital("/a/double"));
}

TEST_CASE("Toggle_flips_on_each_press")
{
	const std::string click = "/user/hand/right/input/a/click";
	Runner r(Source("toggle_button", { { "click", "/t" } }, {}, "/user/hand/right/input/a"));
	std::vector<bool> states;
	for (float v : { 0.f, 1.f, 0.f, 1.f, 0.f }) {
		r.Step({ { click, v } });
		states.push_back(r.Digital("/t"));
	}
	CHECK(states == std::vector<bool>{ false, true, true, false, false });
}

TEST_CASE("Toggle_on_a_force_only_pad")
{
	// No click or value on the source (Index's trackpad): the force presses it
	const std::string force = "/user/hand/right/input/trackpad/force";
	Runner r(Source("toggle_button", { { "click", "/t" } }, {}, "/user/hand/right/input/trackpad"));
	r.Step({ { force, 0.0f } });
	CHECK_FALSE(r.Digital("/t"));
	r.Step({ { force, 0.5f } });
	CHECK(r.Digital("/t"));
	r.Step({ { force, 0.0f } });
	r.Step({ { force, 0.5f } });
	CHECK_FALSE(r.Digital("/t"));
}

TEST_CASE("Trigger_pull_and_click_with_hysteresis")
{
	const std::string value = kSrc + "/value";
	Runner r(Source("trigger", { { "pull", "/pull" }, { "click", "/click" }, { "touch", "/touch" } }));
	float on = ModeParameterDefault("trigger", "click_activate_threshold");
	float off = ModeParameterDefault("trigger", "click_deactivate_threshold");
	REQUIRE(on > off);

	r.Step({ { value, 0.5f } });
	CHECK(r.Analog("/pull") == 0.5f);
	CHECK(r.Digital("/touch"));
	CHECK_FALSE(r.Digital("/click"));
	r.Step({ { value, on } });
	CHECK(r.Digital("/click"));
	r.Step({ { value, (on + off) / 2 } }); // between: stays on
	CHECK(r.Digital("/click"));
	r.Step({ { value, off } });
	CHECK_FALSE(r.Digital("/click"));
	r.Step({ { value, (on + off) / 2 } }); // between: stays off
	CHECK_FALSE(r.Digital("/click"));
	CHECK(std::fabs(r.out.analog["/pull"].dx) > 0);

	// The file's own thresholds
	Runner custom(Source("trigger", { { "click", "/click" } }, { { "click_activate_threshold", "0.3" }, { "click_deactivate_threshold", "0.2" } }));
	custom.Step({ { value, 0.35f } });
	CHECK(custom.Digital("/click"));
}

TEST_CASE("Grab_hold_and_release_thresholds")
{
	const std::string value = "/user/hand/right/input/grip/value", force = "/user/hand/right/input/grip/force";
	Runner r(Source("grab", { { "grab", "/grab" } }, {}, "/user/hand/right/input/grip"));
	r.Step({ { value, 0.69f } });
	CHECK_FALSE(r.Digital("/grab"));
	r.Step({ { value, 0.7f } });
	CHECK(r.Digital("/grab"));
	r.Step({ { value, 0.66f } });
	CHECK(r.Digital("/grab"));
	r.Step({ { value, 0.65f } });
	CHECK_FALSE(r.Digital("/grab"));
	// Or squeezed hard enough
	r.Step({ { value, 0.1f }, { force, ModeParameterDefault("grab", "force_hold_threshold") } });
	CHECK(r.Digital("/grab"));
}

TEST_CASE("Grab_on_a_grip_with_only_a_click")
{
	const std::string click = "/user/hand/right/input/grip/click";
	Runner r(Source("grab", { { "grab", "/grab" } }, {}, "/user/hand/right/input/grip"));
	r.Step({ { click, 1.0f } });
	CHECK(r.Digital("/grab"));
	r.Step({ { click, 0.0f } });
	CHECK_FALSE(r.Digital("/grab"));
}

TEST_CASE("Scalar_constant_outputs_its_value")
{
	Runner r(Source("scalar_constant", { { "value", "/speed" } }, { { "value", "0.25" } }));
	r.Step({});
	CHECK(r.Analog("/speed") == 0.25f);
	Runner byDefault(Source("scalar_constant", { { "value", "/speed" } }));
	byDefault.Step({});
	CHECK(byDefault.Analog("/speed") == 1.0f);
}

TEST_CASE("Force_sensor_outputs_force")
{
	const std::string force = "/user/hand/right/input/grip/force";
	Runner r(Source("force_sensor", { { "force", "/squeeze" } }, {}, "/user/hand/right/input/grip"));
	r.Step({ { force, 0.4f } });
	CHECK(r.Analog("/squeeze") == 0.4f);
}

TEST_CASE("Complex_button_long_press")
{
	const std::string click = "/user/hand/right/input/b/click";
	Runner r(Source("complex_button", { { "long", "/long" }, { "click", "/click" } }, { { "long_press_delay", "0.5" } }, "/user/hand/right/input/b"));
	for (int i = 0; i < 5; i++) // 0.4 s held
		r.Step({ { click, 1 } }, 0.1);
	CHECK(r.Digital("/click"));
	CHECK_FALSE(r.Digital("/long"));
	r.Step({ { click, 1 } }, 0.1);
	r.Step({ { click, 1 } }, 0.1);
	CHECK(r.Digital("/long"));
	r.Step({ { click, 0 } }, 0.1);
	CHECK_FALSE(r.Digital("/long"));
}

TEST_CASE("Unknown_mode_warns")
{
	std::vector<std::string> warnings;
	CHECK(CreateModeBinding(Source("telepathy", { { "click", "/x" } }), warnings) == nullptr);
	CHECK(warnings.size() == 1);
}

namespace {
const std::string kPad = "/user/hand/right/input/trackpad";
const std::string kStick = "/user/hand/right/input/joystick";
std::map<std::string, double> At(const std::string& path, float x, float y, bool touch = true, bool click = false)
{
	return { { path + "/x", x }, { path + "/y", y }, { path + "/touch", touch ? 1.0 : 0.0 }, { path + "/click", click ? 1.0 : 0.0 } };
}
} // namespace

TEST_CASE("Trackpad_position_touch_click")
{
	Runner r(Source("trackpad", { { "position", "/pos" }, { "touch", "/touch" }, { "click", "/click" } }, {}, kPad));
	r.Step(At(kPad, 0.3f, -0.4f, true, false));
	CHECK(r.out.analog["/pos"].x == 0.3f);
	CHECK(r.out.analog["/pos"].y == -0.4f);
	CHECK(r.Digital("/touch"));
	CHECK_FALSE(r.Digital("/click"));
	r.Step(At(kPad, 0.5f, -0.4f, true, true));
	CHECK(r.Digital("/click"));
	CHECK(r.out.analog["/pos"].dx == doctest::Approx(0.2f));
	r.Step(At(kPad, 0, 0, false, false));
	CHECK_FALSE(r.Digital("/touch"));
}

TEST_CASE("Joystick_deadzone_and_maxzone")
{
	Runner r(Source("joystick", { { "position", "/move" } }, { { "deadzone_pct", "10" }, { "maxzone_pct", "90" } }, kStick));
	r.Step(At(kStick, 0.05f, 0, false));
	CHECK(r.out.analog["/move"].x == 0.0f);
	r.Step(At(kStick, 0.5f, 0));
	CHECK(r.out.analog["/move"].x == doctest::Approx(0.5f));
	r.Step(At(kStick, 0.95f, 0));
	CHECK(r.out.analog["/move"].x == doctest::Approx(1.0f));
	// Radially: the direction kept, the length scaled
	float d = 0.5f / std::sqrt(2.0f);
	r.Step(At(kStick, d, d));
	CHECK(r.out.analog["/move"].x == doctest::Approx(d).epsilon(0.001));
	CHECK(r.out.analog["/move"].y == doctest::Approx(d).epsilon(0.001));
}

TEST_CASE("Dpad_quadrants_with_deadzone")
{
	Runner r(Source("dpad", { { "north", "/n" }, { "east", "/e" }, { "south", "/s" }, { "west", "/w" } }, { { "sub_mode", "touch" } }, kPad));
	r.Step(At(kPad, 0, 0.9f));
	CHECK(r.Digital("/n"));
	CHECK_FALSE(r.Digital("/e"));
	r.Step(At(kPad, 0.9f, 0.1f));
	CHECK(r.Digital("/e"));
	CHECK_FALSE(r.Digital("/n"));
	r.Step(At(kPad, 0, -0.9f));
	CHECK(r.Digital("/s"));
	r.Step(At(kPad, -0.9f, 0));
	CHECK(r.Digital("/w"));
	r.Step(At(kPad, 0, 0.3f)); // inside the 50 % deadzone: nothing
	CHECK_FALSE(r.Digital("/n"));
	r.Step(At(kPad, 0, 0.9f, false)); // not touched: nothing
	CHECK_FALSE(r.Digital("/n"));

	// Overlap: a diagonal gives both
	Runner overlap(Source("dpad", { { "north", "/n" }, { "east", "/e" } }, { { "overlap_pct", "20" } }, kPad));
	overlap.Step(At(kPad, 0.7f, 0.7f));
	CHECK(overlap.Digital("/n"));
	CHECK(overlap.Digital("/e"));
}

TEST_CASE("Dpad_center_with_click")
{
	Runner r(Source("dpad", { { "center", "/c" }, { "north", "/n" } }, { { "sub_mode", "click" } }, kPad));
	r.Step(At(kPad, 0.1f, 0.1f, true, false));
	CHECK_FALSE(r.Digital("/c")); // touched, not clicked
	r.Step(At(kPad, 0.1f, 0.1f, true, true));
	CHECK(r.Digital("/c"));
	CHECK_FALSE(r.Digital("/n"));
	r.Step(At(kPad, 0, 0.9f, true, true));
	CHECK(r.Digital("/n"));
	CHECK_FALSE(r.Digital("/c"));
}

TEST_CASE("Dpad_touch_sub_mode")
{
	Runner r(Source("dpad", { { "west", "/w" } }, { { "sub_mode", "touch" } }, kPad));
	r.Step(At(kPad, -0.8f, 0, true, false));
	CHECK(r.Digital("/w"));
	r.Step(At(kPad, -0.8f, 0, false, false));
	CHECK_FALSE(r.Digital("/w"));
}

TEST_CASE("Scroll_discrete_steps")
{
	Runner r(Source("scroll", { { "scroll", "/scroll" } }, { { "scroll_mode", "discrete" }, { "discrete_scroll_step", "0.2" } }, kPad));
	float steps = 0;
	r.Step(At(kPad, 0, -0.5f));
	for (float y = -0.4f; y <= 0.31f; y += 0.1f) { // 0.8 up, in tenths
		r.Step(At(kPad, 0, y));
		steps += r.out.analog["/scroll"].y;
	}
	CHECK(steps == 4.0f);
	r.Step(At(kPad, 0, 0.3f, false)); // lifted
	CHECK(r.out.analog["/scroll"].y == 0.0f);
}

TEST_CASE("Scroll_smooth_delta")
{
	Runner r(Source("scroll", { { "scroll", "/scroll" } }, { { "scroll_mode", "smooth" }, { "smooth_scroll_multiplier", "2" } }, kPad));
	r.Step(At(kPad, 0, 0));
	CHECK(r.out.analog["/scroll"].y == 0.0f); // the first touch moves nothing
	r.Step(At(kPad, 0.1f, 0.25f));
	CHECK(r.out.analog["/scroll"].y == doctest::Approx(0.5f));
	CHECK(r.out.analog["/scroll"].x == doctest::Approx(0.2f));
}

TEST_CASE("Radial_segments")
{
	Runner r(Source("radial", { { "0", "/top" }, { "1", "/right" }, { "2", "/bottom" }, { "3", "/left" } }, { { "segments", "4" } }, kPad));
	r.Step(At(kPad, 0, 0.9f));
	CHECK(r.Digital("/top"));
	r.Step(At(kPad, 0.9f, 0));
	CHECK(r.Digital("/right"));
	CHECK_FALSE(r.Digital("/top"));
	r.Step(At(kPad, 0, -0.9f));
	CHECK(r.Digital("/bottom"));
	r.Step(At(kPad, -0.9f, 0.05f));
	CHECK(r.Digital("/left"));
	r.Step(At(kPad, 0, 0.1f)); // the centre
	CHECK_FALSE(r.Digital("/top"));
	CHECK(r.mode->DigitalInput("0") == false);
}

TEST_CASE("Trackpad_click_from_force")
{
	// Index's trackpad has a force sensor and no click: its click comes from the force, through thresholds
	const std::string pad = "/user/hand/right/input/trackpad";
	Runner r(Source("trackpad", { { "click", "/click" } }, {}, pad));
	r.Step({ { pad + "/x", 0 }, { pad + "/y", 0 }, { pad + "/touch", 1 }, { pad + "/force", 0.1 } });
	CHECK_FALSE(r.Digital("/click"));
	r.Step({ { pad + "/x", 0 }, { pad + "/y", 0 }, { pad + "/touch", 1 }, { pad + "/force", 0.9 } });
	CHECK(r.Digital("/click"));
	r.Step({ { pad + "/x", 0 }, { pad + "/y", 0 }, { pad + "/touch", 1 }, { pad + "/force", 0.0 } });
	CHECK_FALSE(r.Digital("/click"));

	// A button on it (no force_input) the same way, as SteamVR's Index legacy file binds it
	Runner b(Source("button", { { "click", "/press" } }, {}, pad));
	b.Step({ { pad + "/touch", 1 }, { pad + "/force", 0.9 } });
	CHECK(b.Digital("/press"));
	b.Step({ { pad + "/touch", 1 }, { pad + "/force", 0.0 } });
	CHECK_FALSE(b.Digital("/press"));
}
