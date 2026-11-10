#pragma once

// A binding file evaluated: every source of the active action sets through its mode, chords, simulated actions,
// several bindings on one action combined, and a higher-priority set's inputs kept from lower ones.

#include "BindingFile.h"
#include "Modes.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class BindingEvaluator {
public:
	explicit BindingEvaluator(const BindingFile& file);

	/** The sets UpdateActionState made active, with their priorities (higher wins) */
	void SetActiveActionSets(const std::vector<std::pair<std::string, int32_t>>& setsWithPriority);

	/** One frame: every active binding reads the raw inputs, and the actions' outputs are combined */
	void Update(const RawInputState& in);

	/** This frame's actions: an action of no active binding isn't there (inactive) */
	const ActionOutputs& Outputs() const { return outputs; }

	/** Every raw input any binding reads */
	std::vector<std::string> RawInputs() const;

	struct HapticTarget {
		std::string path;
		float amplitudeScale = 1.0f;
	};
	/** Where a haptic action goes, from the active sets' haptics, and its amplitude parameter */
	std::optional<HapticTarget> HapticFor(const std::string& actionPath) const;

	const std::vector<std::string>& Warnings() const { return warnings; }

private:
	struct Set {
		std::string name;
		std::vector<std::unique_ptr<ModeBinding>> modes;
		std::vector<std::string> modePaths; // each mode's source path
		std::vector<ChordBinding> chords;
		std::vector<HapticBinding> haptics;
	};
	std::vector<Set> sets;
	std::vector<SimulatedAction> simulated;
	std::vector<std::pair<std::string, int32_t>> active;
	ActionOutputs outputs, previous;
	std::vector<std::string> warnings;
};
