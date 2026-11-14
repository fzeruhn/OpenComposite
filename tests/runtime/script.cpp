#include "script.h"

#include <openxr/openxr_reflection.h>

#include <cstdlib>
#include <fstream>

static XrResult ResultFromName(const std::string& name)
{
#define OC_RESULT_CASE(n, v) \
	if (name == #n)          \
		return n;
	XR_LIST_ENUM_XrResult(OC_RESULT_CASE);
#undef OC_RESULT_CASE
	return XR_ERROR_RUNTIME_FAILURE;
}

static Script LoadScript()
{
	Script s;
	const char* path = std::getenv("OC_TEST_RUNTIME_SCRIPT");
	if (!path || !*path)
		return s;

	std::ifstream in(path);
	Json::Value root;
	Json::CharReaderBuilder rb;
	rb["allowComments"] = true;
	std::string errs;
	if (!Json::parseFromStream(rb, in, &root, &errs))
		return s;

	if (root.isMember("system_name"))
		s.systemName = root["system_name"].asString();
	if (root.isMember("extensions")) {
		s.extensions.clear();
		for (const Json::Value& e : root["extensions"])
			s.extensions.insert(e.asString());
	}
	if (root.isMember("view_size")) {
		s.viewWidth = root["view_size"][0].asUInt();
		s.viewHeight = root["view_size"][1].asUInt();
	}
	if (root.isMember("fov_deg"))
		s.fovDeg = root["fov_deg"].asFloat();
	if (root.isMember("ipd"))
		s.ipd = root["ipd"].asFloat();
	if (root.isMember("display_hz"))
		s.displayHz = root["display_hz"].asFloat();
	if (root.isMember("stage_bounds")) {
		s.stageWidth = root["stage_bounds"][0].asFloat();
		s.stageDepth = root["stage_bounds"][1].asFloat();
	}
	for (const Json::Value& e : root["events"]) {
		ScriptEvent ev;
		ev.frame = e["frame"].asUInt64();
		ev.type = e["type"].asString();
		ev.state = e["state"].asString();
		s.events.push_back(ev);
	}
	for (const Json::Value& f : root["fail"]) {
		ScriptFailure fail;
		fail.function = f["function"].asString();
		fail.result = ResultFromName(f["result"].asString());
		fail.count = f.isMember("count") ? f["count"].asInt() : 1;
		fail.after = f.isMember("after") ? f["after"].asInt() : 0;
		s.failures.push_back(fail);
	}
	s.interactionProfile = root["interaction_profile"].asString();
	for (const Json::Value& i : root["inputs"]) {
		ScriptInput in2;
		in2.frame = i["frame"].asUInt64();
		in2.path = i["path"].asString();
		in2.value = i["value"].asFloat();
		s.inputs.push_back(in2);
	}
	for (const Json::Value& p : root["presence"]) {
		ScriptPresence pr;
		pr.frame = p["frame"].asUInt64();
		pr.worn = p["worn"].asBool();
		s.presence.push_back(pr);
	}
	return s;
}

const Script& GetScript()
{
	static Script script = LoadScript();
	return script;
}

bool ScriptFails(const char* function, XrResult* result)
{
	static std::mutex lock;
	static std::map<std::string, int> calls;
	static std::map<const ScriptFailure*, int> used;
	std::lock_guard<std::mutex> guard(lock);
	int call = ++calls[function]; // this call's number, from 1
	for (const ScriptFailure& f : GetScript().failures) {
		if (f.function != function)
			continue;
		// The first `after` calls succeed, then `count` fail
		if (call <= f.after)
			continue;
		int& n = used[&f];
		if (n >= f.count)
			continue;
		n++;
		*result = f.result;
		Dump& d = GetDump();
		std::lock_guard<std::mutex> dg(d.lock);
		Json::Value entry;
		entry["function"] = function;
		entry["result"] = (int)f.result;
		entry["call"] = call;
		d.root["failed_calls"].append(entry);
		return true;
	}
	return false;
}

Dump& GetDump()
{
	static Dump dump;
	return dump;
}

void Dump::Write()
{
	const char* path = std::getenv("OC_TEST_RUNTIME_DUMP");
	if (!path || !*path)
		return;
	std::lock_guard<std::mutex> guard(lock);
	std::ofstream out(path, std::ios::trunc);
	Json::StreamWriterBuilder wb;
	wb["indentation"] = " ";
	out << Json::writeString(wb, root) << "\n";
}
