#pragma once

// A binding file evaluated: every source of the active action sets through its mode, chords, simulated actions,
// several bindings on one action combined, and a higher-priority set's inputs kept from lower ones.

#include "BindingFile.h"
#include "Modes.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class BindingEvaluator {
public:
	explicit BindingEvaluator(const BindingFile& file);

	/** An active set as UpdateActionState gives it: its priority (higher wins) and the hand it's restricted to
	 * (ulRestrictedToDevice: "/user/hand/left"; empty for every hand). A set may be there once per hand. */
	struct ActiveSet {
		std::string name;
		int32_t priority = 0;
		std::string restrictTo;
	};
	void SetActiveActionSets(const std::vector<ActiveSet>& sets);

	/** One frame: every active binding reads the raw inputs, and the actions' outputs are combined */
	void Update(const RawInputState& in);

	/** This frame's actions: an action of no active binding isn't there (inactive) */
	const ActionOutputs& Outputs() const { return outputs; }
	/** The same from one hand's bindings only ("/user/hand/left"), its changes and deltas against that hand's frame
	 * before: for a read restricted to the hand. Null if none of its bindings wrote an action */
	const ActionOutputs* OutputsOn(const std::string& hand) const;

	/** Every raw input any binding reads */
	std::vector<std::string> RawInputs() const;

	struct HapticTarget {
		std::string path;
		float amplitudeScale = 1.0f;
	};
	/** Where a haptic action goes, from the active sets' haptics (every hand it's bound on, each once, on the hands
	 * its sets are active for), with their amplitude parameter */
	std::vector<HapticTarget> HapticsFor(const std::string& actionPath) const;

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
	std::vector<ActiveSet> active;
	ActionOutputs outputs, previous;
	std::map<std::string, ActionOutputs> handOutputs, handPrevious; // by hand; "" for outputs on none
	std::vector<std::string> warnings;
};
