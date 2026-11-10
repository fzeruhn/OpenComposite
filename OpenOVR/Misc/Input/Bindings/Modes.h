#pragma once

// SteamVR's binding modes: each source binding of a binding file turned into a small state machine that reads the
// controller's raw inputs every frame and writes the actions it's bound to.

#include "BindingFile.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

/** One frame's raw inputs, by SteamVR path: "<source>/click", "/touch", "/value", "/force", "/x", "/y" (for example
 * "/user/hand/right/input/trigger/value") */
struct RawInputState {
	double time = 0; // seconds
	std::unordered_map<std::string, float> value;

	std::optional<float> Get(const std::string& path) const;
};

struct DigitalOut {
	bool state = false;
	bool changed = false; // since the frame before
	double changeTime = 0;
	std::string origin; // the source path
};
struct AnalogOut {
	float x = 0, y = 0, z = 0, dx = 0, dy = 0, dz = 0;
	std::string origin;
};
struct ActionOutputs {
	std::unordered_map<std::string, DigitalOut> digital;
	std::unordered_map<std::string, AnalogOut> analog;
};

class ModeBinding {
public:
	virtual ~ModeBinding() = default;
	/** Reads this frame's inputs, and writes the actions this binding's inputs are bound to into out */
	virtual void Update(const RawInputState& in, ActionOutputs& out) = 0;
	/** The raw input paths it reads */
	virtual std::vector<std::string> RawInputs() const = 0;
	/** This frame's state of one of its inputs (click, north...), bound or not: for chords */
	virtual std::optional<bool> DigitalInput(const std::string& inputName) const = 0;
};

/** The binding for a source's mode; nullptr with a warning for a mode OpenComposite doesn't know */
std::unique_ptr<ModeBinding> CreateModeBinding(const SourceBinding& binding, std::vector<std::string>& warnings);

/** A mode's parameter: its default when the binding file doesn't give it, and where that default comes from */
struct ModeParameter {
	const char* mode;
	const char* name;
	float defaultValue;
	const char* source;
};
extern const std::vector<ModeParameter> kModeParameters;
/** The default for mode's parameter name (0 if the table has none) */
float ModeParameterDefault(const std::string& mode, const std::string& name);
