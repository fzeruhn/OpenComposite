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
	const Json::Value& formats = root["swapchain_formats"];
	if (formats.isArray()) {
		for (const Json::Value& f : formats)
			s.swapchainFormats.push_back(f.asInt64());
	} else if (formats.isObject()) {
		for (const std::string& api : formats.getMemberNames())
			for (const Json::Value& f : formats[api])
				s.swapchainFormatsByApi[api].push_back(f.asInt64());
	}
	if (root["poses"].isObject()) {
		for (const std::string& path : root["poses"].getMemberNames()) {
			const Json::Value& v = root["poses"][path];
			auto add = [&](const Json::Value& e) {
				ScriptPose p;
				p.fromFrame = e["from_frame"].asUInt64();
				if (e["p"].size() == 3)
					p.pose.position = { e["p"][0].asFloat(), e["p"][1].asFloat(), e["p"][2].asFloat() };
				if (e["q"].size() == 4)
					p.pose.orientation = { e["q"][0].asFloat(), e["q"][1].asFloat(), e["q"][2].asFloat(), e["q"][3].asFloat() };
				if (e["v"].size() == 3)
					p.linear = { e["v"][0].asFloat(), e["v"][1].asFloat(), e["v"][2].asFloat() };
				if (e["w"].size() == 3)
					p.angular = { e["w"][0].asFloat(), e["w"][1].asFloat(), e["w"][2].asFloat() };
				s.poses[path].push_back(p);
			};
			if (v.isArray())
				for (const Json::Value& e : v)
					add(e);
			else if (v.isObject())
				add(v);
		}
	}
	for (const Json::Value& r : root["refresh_rates"])
		s.refreshRates.push_back(r.asFloat());
	auto addIpd = [&](const Json::Value& e) {
		ScriptIpd c;
		c.frame = e["frame"].asUInt64();
		c.ipd = e["ipd"].asFloat();
		s.ipdChanges.push_back(c);
	};
	if (root["ipd_change"].isArray())
		for (const Json::Value& e : root["ipd_change"])
			addIpd(e);
	else if (root["ipd_change"].isObject())
		addIpd(root["ipd_change"]);
	return s;
}

const ScriptPose* ScriptPoseAt(const std::string& path, uint64_t frame)
{
	auto it = GetScript().poses.find(path);
	if (it == GetScript().poses.end())
		return nullptr;
	const ScriptPose* best = nullptr;
	for (const ScriptPose& p : it->second)
		if (p.fromFrame <= frame && (!best || p.fromFrame >= best->fromFrame))
			best = &p;
	return best;
}

float ScriptIpdAt(uint64_t frame)
{
	float ipd = GetScript().ipd;
	uint64_t at = 0;
	for (const ScriptIpd& c : GetScript().ipdChanges)
		if (c.frame <= frame && c.frame >= at) {
			ipd = c.ipd;
			at = c.frame;
		}
	return ipd;
}

const Script& GetScript()
{
	static Script script = LoadScript();
	return script;
}

const std::vector<int64_t>* ScriptSwapchainFormats(const std::string& binding)
{
	auto it = GetScript().swapchainFormatsByApi.find(binding);
	if (it != GetScript().swapchainFormatsByApi.end())
		return &it->second;
	return GetScript().swapchainFormats.empty() ? nullptr : &GetScript().swapchainFormats;
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

bool Dump::Enabled()
{
	static const bool enabled = [] {
		const char* path = std::getenv("OC_TEST_RUNTIME_DUMP");
		return path && *path;
	}();
	return enabled;
}

void Dump::Write()
{
	if (!Enabled())
		return;
	std::lock_guard<std::mutex> guard(lock);
	for (const Json::Value& entry : lastFrame)
		root["layers"].append(entry);
	lastFrame = Json::Value(Json::arrayValue);
	std::ofstream out(std::getenv("OC_TEST_RUNTIME_DUMP"), std::ios::trunc);
	Json::StreamWriterBuilder wb;
	wb["indentation"] = " ";
	out << Json::writeString(wb, root) << "\n";
}
