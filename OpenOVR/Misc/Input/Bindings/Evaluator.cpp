#include "stdafx.h"

#include "Evaluator.h"
#include "Misc/Numbers.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>

BindingEvaluator::BindingEvaluator(const BindingFile& file)
    : simulated(file.simulatedActions)
    , warnings(file.warnings)
{
	for (const auto& [name, bindings] : file.sets) {
		Set set;
		set.name = name;
		for (const SourceBinding& source : bindings.sources) {
			std::unique_ptr<ModeBinding> mode = CreateModeBinding(source, warnings);
			if (!mode)
				continue;
			set.modes.push_back(std::move(mode));
			set.modePaths.push_back(source.path);
		}
		set.chords = bindings.chords;
		set.haptics = bindings.haptics;
		sets.push_back(std::move(set));
	}
}

namespace {
// Whether a source or output path is on the hand a set is restricted to (any, if it isn't)
bool OnHand(const std::string& path, const std::string& restrictTo)
{
	return restrictTo.empty() || path == restrictTo || (path.size() > restrictTo.size() && path.compare(0, restrictTo.size(), restrictTo) == 0 && path[restrictTo.size()] == '/');
}

// The hand a source path is on ("/user/hand/left"); empty for a source on none (a simulated action)
std::string HandOf(const std::string& path)
{
	static const std::string prefix = "/user/hand/";
	if (path.compare(0, prefix.size(), prefix) != 0)
		return {};
	return path.substr(0, path.find('/', prefix.size()));
}

// Several bindings' outputs on one action combined, changes and deltas against the frame before
ActionOutputs Combine(const std::vector<std::pair<std::string, DigitalOut>>& digitals, const std::vector<std::pair<std::string, AnalogOut>>& analogs,
    const ActionOutputs& previous, double frameTime)
{
	ActionOutputs outputs;
	// Several bindings on one digital action: on if any is, changed at the latest change
	for (auto& [action, d] : digitals) {
		auto [it, inserted] = outputs.digital.try_emplace(action, d);
		if (!inserted) {
			bool wasOn = it->second.state;
			it->second.state = it->second.state || d.state;
			if (d.state && (!wasOn || d.changeTime >= it->second.changeTime))
				it->second.origin = d.origin;
			it->second.changeTime = std::max(it->second.changeTime, d.changeTime);
		}
	}
	for (auto& [action, d] : outputs.digital) {
		auto before = previous.digital.find(action);
		bool was = before != previous.digital.end() && before->second.state;
		d.changed = d.state != was;
		if (d.changed)
			d.changeTime = frameTime;
		else if (before != previous.digital.end())
			d.changeTime = before->second.changeTime;
	}

	// On an analog action: the binding with the largest value this frame
	for (auto& [action, a] : analogs) {
		auto [it, inserted] = outputs.analog.try_emplace(action, a);
		if (!inserted) {
			float now = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
			float best = std::sqrt(it->second.x * it->second.x + it->second.y * it->second.y + it->second.z * it->second.z);
			if (now > best)
				it->second = a;
		}
	}
	for (auto& [action, a] : outputs.analog) {
		auto before = previous.analog.find(action);
		AnalogOut last = before != previous.analog.end() ? before->second : AnalogOut{};
		a.dx = a.x - last.x;
		a.dy = a.y - last.y;
		a.dz = a.z - last.z;
	}
	return outputs;
}
} // namespace

void BindingEvaluator::SetActiveActionSets(const std::vector<ActiveSet>& sets)
{
	active.clear();
	for (ActiveSet set : sets) {
		std::transform(set.name.begin(), set.name.end(), set.name.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		active.push_back(std::move(set));
	}
}

void BindingEvaluator::Update(const RawInputState& in)
{
	previous = std::move(outputs);
	outputs = ActionOutputs{};

	// The active sets, highest priority first; a set's bindings on raw inputs a higher set reads are left out. A set
	// restricted to a hand has only its bindings on that hand (and claims only their inputs).
	struct Entry {
		const Set* set;
		int32_t priority;
		const std::string* restrictTo;
	};
	std::vector<Entry> order;
	for (const ActiveSet& a : active) {
		for (const Set& s : sets)
			if (s.name == a.name)
				order.push_back({ &s, a.priority, &a.restrictTo });
	}
	std::stable_sort(order.begin(), order.end(), [](const Entry& a, const Entry& b) { return a.priority > b.priority; });
	std::set<const ModeBinding*> ran; // a set active for two hands (or twice) runs each binding once a frame

	std::set<std::string> claimed; // raw inputs read by a set of higher priority
	std::vector<std::pair<std::string, DigitalOut>> digitals;
	std::vector<std::pair<std::string, AnalogOut>> analogs;
	size_t i = 0;
	while (i < order.size()) {
		// Sets of one priority don't block each other
		int32_t priority = order[i].priority;
		std::set<std::string> readHere;
		for (; i < order.size() && order[i].priority == priority; i++) {
			const Set& s = *order[i].set;
			const std::string& restrictTo = *order[i].restrictTo;
			for (size_t m = 0; m < s.modes.size(); m++) {
				ModeBinding& mode = *s.modes[m];
				if (!OnHand(s.modePaths[m], restrictTo) || ran.count(&mode))
					continue;
				std::vector<std::string> raw = mode.RawInputs();
				bool blocked = std::any_of(raw.begin(), raw.end(), [&](const std::string& r) { return claimed.count(r) != 0; });
				// None of its raw inputs active (the hand's controller is another, or the session isn't focused): the
				// mode doesn't run, so its actions read as inactive, as they do without the evaluator
				bool present = raw.empty() || std::any_of(raw.begin(), raw.end(), [&](const std::string& r) { return in.value.count(r) != 0; });
				if (blocked || !present)
					continue;
				readHere.insert(raw.begin(), raw.end());
				ran.insert(&mode);
				ActionOutputs one;
				mode.Update(in, one);
				for (auto& d : one.digital)
					digitals.push_back(d);
				for (auto& a : one.analog)
					analogs.push_back(a);
			}

			// Chords: true while every (source, input) is; an input no mode that ran this frame answers, while one on its
			// source didn't run (its inputs gone, or a higher set's), makes the chord inactive, as that source's own
			// bindings are
			for (const ChordBinding& chord : s.chords) {
				if (!std::all_of(chord.inputs.begin(), chord.inputs.end(), [&](const auto& in) { return OnHand(in.first, restrictTo); }))
					continue;
				bool all = true, inactive = false;
				for (const auto& [path, input] : chord.inputs) {
					bool found = false, skipped = false;
					for (size_t m = 0; m < s.modes.size() && !found; m++) {
						if (s.modePaths[m] != path)
							continue;
						if (!ran.count(&*s.modes[m])) {
							skipped = true;
							continue;
						}
						std::optional<bool> st = s.modes[m]->DigitalInput(input);
						if (st) {
							found = true;
							all = all && *st;
						}
					}
					all = all && found;
					inactive = inactive || (!found && skipped);
				}
				if (inactive)
					continue;
				DigitalOut d;
				d.state = all;
				d.origin = chord.inputs.empty() ? std::string() : chord.inputs.front().first;
				digitals.emplace_back(chord.output, d);
			}
		}
		claimed.insert(readHere.begin(), readHere.end());
	}

	// Simulated actions: a constant value for the action (Valve doesn't document them; an estimate, not measured)
	for (const SimulatedAction& sim : simulated) {
		const Json::Value& value = sim.raw["value"];
		if (sim.type == "boolean" || sim.type == "click") {
			DigitalOut d;
			d.state = value.isNull() || !value.isConvertibleTo(Json::booleanValue) ? true : value.asBool();
			digitals.emplace_back(sim.name, d);
		} else if (sim.type == "vector1" || sim.type == "vector2" || sim.type == "vector3") {
			AnalogOut a;
			a.x = value.isNumeric() ? value.asFloat() : 1.0f;
			analogs.emplace_back(sim.name, a);
		}
	}

	outputs = Combine(digitals, analogs, previous, in.time);

	// The same for each hand's own bindings, for a read restricted to a hand (ulRestrictToDevice): one hand's state,
	// change and deltas aren't the other's
	std::map<std::string, std::vector<std::pair<std::string, DigitalOut>>> handDigitals;
	std::map<std::string, std::vector<std::pair<std::string, AnalogOut>>> handAnalogs;
	for (auto& d : digitals)
		handDigitals[HandOf(d.second.origin)].push_back(d);
	for (auto& a : analogs)
		handAnalogs[HandOf(a.second.origin)].push_back(a);
	handPrevious = std::move(handOutputs);
	handOutputs.clear();
	std::set<std::string> hands;
	for (const auto& d : handDigitals)
		hands.insert(d.first);
	for (const auto& a : handAnalogs)
		hands.insert(a.first);
	static const ActionOutputs none;
	for (const std::string& hand : hands) {
		auto before = handPrevious.find(hand);
		handOutputs[hand] = Combine(handDigitals[hand], handAnalogs[hand], before != handPrevious.end() ? before->second : none, in.time);
	}
}

const ActionOutputs* BindingEvaluator::OutputsOn(const std::string& hand) const
{
	auto it = handOutputs.find(hand);
	return it == handOutputs.end() ? nullptr : &it->second;
}

std::vector<std::string> BindingEvaluator::RawInputs() const
{
	std::set<std::string> raw;
	for (const Set& s : sets)
		for (const auto& m : s.modes)
			for (const std::string& r : m->RawInputs())
				raw.insert(r);
	return { raw.begin(), raw.end() };
}

std::vector<BindingEvaluator::HapticTarget> BindingEvaluator::HapticsFor(const std::string& actionPath) const
{
	std::string lower = actionPath;
	std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	std::vector<HapticTarget> targets;
	for (const ActiveSet& a : active) {
		for (const Set& s : sets) {
			if (s.name != a.name)
				continue;
			for (const HapticBinding& h : s.haptics) {
				if (h.output != lower || !OnHand(h.path, a.restrictTo))
					continue;
				if (std::any_of(targets.begin(), targets.end(), [&](const HapticTarget& t) { return t.path == h.path; }))
					continue;
				HapticTarget t;
				t.path = h.path;
				// The binding's amplitude parameter scales what the game asks for
				auto amp = h.parameters.find("amplitude");
				if (amp == h.parameters.end())
					amp = h.parameters.find("haptic_amplitude");
				if (amp != h.parameters.end()) {
					if (std::optional<float> v = Numbers::ParseFloat(amp->second))
						t.amplitudeScale = *v;
				}
				targets.push_back(std::move(t));
			}
		}
	}
	return targets;
}
