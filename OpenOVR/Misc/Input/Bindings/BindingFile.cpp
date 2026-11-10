#include "stdafx.h"

#include "BindingFile.h"

#include "Misc/JsonFile.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <set>

namespace {

std::string Lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return s;
}

// A parameter as text: SteamVR's files mostly quote them ("0.7"), some don't
std::string ParameterText(const Json::Value& v)
{
	if (v.isString())
		return v.asString();
	if (v.isBool())
		return v.asBool() ? "true" : "false";
	if (v.isInt64())
		return std::to_string(v.asInt64());
	if (v.isNumeric()) {
		char buffer[32];
		snprintf(buffer, sizeof(buffer), "%.9g", v.asDouble()); // 0.6, not 0.59999999999999998
		return buffer;
	}
	return {};
}

std::map<std::string, std::string> Parameters(const Json::Value& v, const std::string& where, std::vector<std::string>& warnings)
{
	std::map<std::string, std::string> out;
	if (v.isNull())
		return out;
	if (!v.isObject()) {
		warnings.push_back(where + ": parameters isn't an object, ignored");
		return out;
	}
	for (const std::string& name : v.getMemberNames())
		out[Lower(name)] = ParameterText(v[name]);
	return out;
}

std::string StringField(const Json::Value& v, const char* name)
{
	return v.isObject() && v[name].isString() ? v[name].asString() : std::string();
}

// The fields of an entry we read; any other is warned about
void WarnUnknown(const Json::Value& v, const std::set<std::string>& known, const std::string& where, std::vector<std::string>& warnings)
{
	if (!v.isObject())
		return;
	for (const std::string& name : v.getMemberNames())
		if (!known.count(name))
			warnings.push_back(where + ": unknown field '" + name + "', skipped");
}

void ReadSet(const std::string& setName, const Json::Value& v, ActionSetBindings& set, std::vector<std::string>& warnings)
{
	if (!v.isObject()) {
		warnings.push_back(setName + ": not an object, skipped");
		return;
	}
	for (const std::string& section : v.getMemberNames()) {
		const Json::Value& list = v[section];
		std::string where = setName + "/" + section;
		if (section != "sources" && section != "poses" && section != "haptics" && section != "skeleton" && section != "chords") {
			warnings.push_back(where + ": unknown section, skipped");
			continue;
		}
		if (!list.isArray()) {
			warnings.push_back(where + ": not an array, skipped");
			continue;
		}
		for (const Json::Value& e : list) {
			if (!e.isObject()) {
				warnings.push_back(where + ": an entry that isn't an object, skipped");
				continue;
			}
			if (section == "sources") {
				WarnUnknown(e, { "path", "mode", "parameters", "inputs", "disabled" }, where, warnings);
				const Json::Value& disabled = e["disabled"];
				if (!disabled.isConvertibleTo(Json::booleanValue))
					warnings.push_back(where + ": \"disabled\" isn't a boolean, taken as false");
				else if (disabled.asBool())
					continue;
				SourceBinding s;
				s.path = Lower(StringField(e, "path"));
				s.mode = Lower(StringField(e, "mode"));
				s.parameters = Parameters(e["parameters"], where, warnings);
				const Json::Value& inputs = e["inputs"];
				if (inputs.isObject()) {
					for (const std::string& name : inputs.getMemberNames()) {
						std::string output = StringField(inputs[name], "output");
						if (output.empty()) {
							warnings.push_back(where + " " + s.path + ": input '" + name + "' has no output, skipped");
							continue;
						}
						s.inputs[Lower(name)] = Lower(output);
					}
				}
				if (s.path.empty() || s.mode.empty()) {
					warnings.push_back(where + ": a source without a path or mode, skipped");
					continue;
				}
				set.sources.push_back(std::move(s));
			} else if (section == "poses" || section == "haptics") {
				WarnUnknown(e, { "output", "path", "parameters", "requirement" }, where, warnings);
				std::string output = Lower(StringField(e, "output")), path = Lower(StringField(e, "path"));
				if (output.empty() || path.empty()) {
					warnings.push_back(where + ": an entry without an output or path, skipped");
					continue;
				}
				if (section == "poses")
					set.poses.push_back({ output, path, Parameters(e["parameters"], where, warnings) });
				else
					set.haptics.push_back({ output, path, Parameters(e["parameters"], where, warnings) });
			} else if (section == "skeleton") {
				WarnUnknown(e, { "output", "path" }, where, warnings);
				std::string output = Lower(StringField(e, "output")), path = Lower(StringField(e, "path"));
				if (output.empty() || path.empty()) {
					warnings.push_back(where + ": an entry without an output or path, skipped");
					continue;
				}
				set.skeleton.push_back({ output, path });
			} else {
				WarnUnknown(e, { "output", "inputs" }, where, warnings);
				ChordBinding chord;
				chord.output = Lower(StringField(e, "output"));
				if (e["inputs"].isArray()) {
					for (const Json::Value& pair : e["inputs"]) {
						if (pair.isArray() && pair.size() == 2 && pair[0].isString() && pair[1].isString())
							chord.inputs.emplace_back(Lower(pair[0].asString()), Lower(pair[1].asString()));
						else
							warnings.push_back(where + ": a chord input that isn't [path, input], skipped");
					}
				}
				if (chord.output.empty() || chord.inputs.empty()) {
					warnings.push_back(where + ": a chord without an output or inputs, skipped");
					continue;
				}
				set.chords.push_back(std::move(chord));
			}
		}
	}
}

} // namespace

BindingFile ParseBindingFile(std::string_view text)
{
	BindingFile file;
	Json::Value root;
	std::string error;
	if (!ParseJsonLenient(text, root, error)) {
		file.warnings.push_back("not a binding file: " + error);
		return file;
	}
	if (!root.isObject()) {
		file.warnings.push_back("not a binding file: not a JSON object");
		return file;
	}

	// The top level's other fields describe the file for SteamVR's binding UI
	static const std::set<std::string> known = { "controller_type", "bindings", "simulated_actions", "action_manifest_version",
		"alias_info", "app_key", "category", "description", "name", "options", "interaction_profile", "publish_options",
		"remote_url", "binding_url", "last_edited_by", "localization", "version" };
	WarnUnknown(root, known, "binding file", file.warnings);

	file.controllerType = Lower(StringField(root, "controller_type"));

	const Json::Value& bindings = root["bindings"];
	if (!bindings.isObject()) {
		file.warnings.push_back("binding file: no bindings object");
	} else {
		for (const std::string& setName : bindings.getMemberNames())
			ReadSet(Lower(setName), bindings[setName], file.sets[Lower(setName)], file.warnings);
	}

	const Json::Value& simulated = root["simulated_actions"];
	if (simulated.isArray()) {
		for (const Json::Value& a : simulated) {
			if (!a.isObject()) {
				file.warnings.push_back("simulated_actions: an entry that isn't an object, skipped");
				continue;
			}
			file.simulatedActions.push_back({ Lower(StringField(a, "name")), Lower(StringField(a, "type")), a });
		}
	} else if (!simulated.isNull()) {
		file.warnings.push_back("simulated_actions: not an array, skipped");
	}
	return file;
}

BindingFile LoadBindingFile(const std::filesystem::path& path)
{
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		BindingFile file;
		std::u8string p = path.u8string();
		file.warnings.push_back("can't open " + std::string(p.begin(), p.end()));
		return file;
	}
	std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	return ParseBindingFile(text);
}
