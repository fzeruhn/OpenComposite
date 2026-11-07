#pragma once
// The stand-in runtime's script (OC_TEST_RUNTIME_SCRIPT) and dump (OC_TEST_RUNTIME_DUMP).

#include <openxr/openxr.h>

#include <json/json.h>

#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

struct ScriptEvent {
	uint64_t frame = 0;
	std::string type; // "session_state"
	std::string state; // "LOSS_PENDING", "EXITING", "STOPPING", ...
};

struct ScriptFailure {
	std::string function;
	XrResult result = XR_ERROR_RUNTIME_FAILURE;
	int count = 1;
};

struct ScriptInput {
	uint64_t frame = 0;
	std::string path;
	float value = 0;
};

struct ScriptPresence {
	uint64_t frame = 0;
	bool worn = true;
};

struct Script {
	std::string systemName = "OpenComposite test runtime";
	std::set<std::string> extensions = { "XR_KHR_D3D11_enable" };
	uint32_t viewWidth = 1920, viewHeight = 1920;
	float fovDeg = 45;
	float ipd = 0.063f;
	float displayHz = 90;
	float stageWidth = 2.0f, stageDepth = 3.0f;
	std::vector<ScriptEvent> events;
	std::vector<ScriptFailure> failures;
	std::string interactionProfile;
	std::vector<ScriptInput> inputs;
	std::vector<ScriptPresence> presence;
};

// Loaded once from OC_TEST_RUNTIME_SCRIPT (defaults without it)
const Script& GetScript();

// Returns true (and sets *result) if the script makes this call to function fail
bool ScriptFails(const char* function, XrResult* result);

// The dump, written to OC_TEST_RUNTIME_DUMP at xrDestroyInstance
struct Dump {
	std::mutex lock;
	Json::Value root{ Json::objectValue };
	void Write();
};
Dump& GetDump();
