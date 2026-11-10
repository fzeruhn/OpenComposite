#include "stdafx.h"

#include "Evaluator.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
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

void BindingEvaluator::SetActiveActionSets(const std::vector<std::pair<std::string, int32_t>>& setsWithPriority)
{
	active.clear();
	for (const auto& [name, priority] : setsWithPriority) {
		std::string lower = name;
		std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		active.emplace_back(lower, priority);
	}
}

void BindingEvaluator::Update(const RawInputState& in)
{
	previous = std::move(outputs);
	outputs = ActionOutputs{};

	// The active sets, highest priority first; a set's bindings on raw inputs a higher set reads are left out
	std::vector<std::pair<const Set*, int32_t>> order;
	for (const auto& [name, priority] : active) {
		for (const Set& s : sets)
			if (s.name == name)
				order.emplace_back(&s, priority);
	}
	std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.second > b.second; });

	std::set<std::string> claimed; // raw inputs read by a set of higher priority
	std::vector<std::pair<std::string, DigitalOut>> digitals;
	std::vector<std::pair<std::string, AnalogOut>> analogs;
	size_t i = 0;
	while (i < order.size()) {
		// Sets of one priority don't block each other
		int32_t priority = order[i].second;
		std::set<std::string> readHere;
		for (; i < order.size() && order[i].second == priority; i++) {
			const Set& s = *order[i].first;
			for (size_t m = 0; m < s.modes.size(); m++) {
				ModeBinding& mode = *s.modes[m];
				std::vector<std::string> raw = mode.RawInputs();
				bool blocked = std::any_of(raw.begin(), raw.end(), [&](const std::string& r) { return claimed.count(r) != 0; });
				if (blocked)
					continue;
				readHere.insert(raw.begin(), raw.end());
				ActionOutputs one;
				mode.Update(in, one);
				for (auto& d : one.digital)
					digitals.push_back(d);
				for (auto& a : one.analog)
					analogs.push_back(a);
			}

			// Chords: true while every (source, input) is
			for (const ChordBinding& chord : s.chords) {
				bool all = true;
				for (const auto& [path, input] : chord.inputs) {
					bool found = false;
					for (size_t m = 0; m < s.modes.size() && !found; m++) {
						if (s.modePaths[m] != path)
							continue;
						std::optional<bool> st = s.modes[m]->DigitalInput(input);
						if (st) {
							found = true;
							all = all && *st;
						}
					}
					all = all && found;
				}
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
			d.state = value.isNull() ? true : value.asBool();
			digitals.emplace_back(sim.name, d);
		} else if (sim.type == "vector1" || sim.type == "vector2" || sim.type == "vector3") {
			AnalogOut a;
			a.x = value.isNumeric() ? value.asFloat() : 1.0f;
			analogs.emplace_back(sim.name, a);
		}
	}

	// Several bindings on one digital action: on if any is, changed at the latest change
	for (auto& [action, d] : digitals) {
		auto [it, inserted] = outputs.digital.try_emplace(action, d);
		if (!inserted) {
			it->second.state = it->second.state || d.state;
			if (d.state && d.changeTime >= it->second.changeTime)
				it->second.origin = d.origin;
			it->second.changeTime = std::max(it->second.changeTime, d.changeTime);
		}
	}
	for (auto& [action, d] : outputs.digital) {
		auto before = previous.digital.find(action);
		bool was = before != previous.digital.end() && before->second.state;
		d.changed = d.state != was;
		if (d.changed)
			d.changeTime = in.time;
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

std::optional<BindingEvaluator::HapticTarget> BindingEvaluator::HapticFor(const std::string& actionPath) const
{
	std::string lower = actionPath;
	std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	for (const auto& [name, priority] : active) {
		for (const Set& s : sets) {
			if (s.name != name)
				continue;
			for (const HapticBinding& h : s.haptics) {
				if (h.output != lower)
					continue;
				HapticTarget t;
				t.path = h.path;
				// The binding's amplitude parameter scales what the game asks for
				auto amp = h.parameters.find("amplitude");
				if (amp == h.parameters.end())
					amp = h.parameters.find("haptic_amplitude");
				if (amp != h.parameters.end()) {
					char* end = nullptr;
					float v = strtof(amp->second.c_str(), &end);
					if (end != amp->second.c_str())
						t.amplitudeScale = v;
				}
				return t;
			}
		}
	}
	return std::nullopt;
}
