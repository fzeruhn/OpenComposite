// The stand-in runtime's actions: sets, actions, suggested bindings, the current interaction profile (the script's,
// once suggested), action states from the script's inputs, action spaces at fixed hand poses, and haptics.

#include "runtime_internal.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

namespace {

bool ValidPath(const std::string& p)
{
	if (p.empty() || p[0] != '/' || p.back() == '/')
		return false;
	for (char c : p) {
		bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '/' || c == '_' || c == '-' || c == '.';
		if (!ok)
			return false;
	}
	return p.find("//") == std::string::npos;
}

int HandOf(const std::string& path)
{
	if (path.rfind("/user/hand/left", 0) == 0)
		return 0;
	if (path.rfind("/user/hand/right", 0) == 0)
		return 1;
	return -1;
}

// The binding paths of an action in the current profile, filtered by a subaction path
std::vector<std::string> BoundPaths(RtSession* s, RtAction* action, XrPath subaction)
{
	std::vector<std::string> out;
	auto it = St().suggested.find(s->currentProfile);
	if (it == St().suggested.end())
		return out;
	std::string prefix = subaction != XR_NULL_PATH ? RtPathString(subaction) : "";
	for (auto& [a, path] : it->second) {
		if (a != action)
			continue;
		std::string p = RtPathString(path);
		if (!prefix.empty() && p.rfind(prefix, 0) != 0)
			continue;
		out.push_back(p);
	}
	return out;
}

float InputValue(RtSession* s, const std::string& path)
{
	auto it = s->inputValues.find(path);
	return it == s->inputValues.end() ? 0.0f : it->second;
}

// Action calls given a session handle that isn't the live session's (destroyed, or none yet): on a real runtime, a use
// after free. Dumped as action_calls_without_session.
std::atomic<uint64_t> callsWithoutSession{ 0 };

RtSession* Session(XrSession session)
{
	RtSession* s = St().session.get();
	if (s && (XrSession)s == session)
		return s;
	callsWithoutSession++;
	return nullptr;
}

XrResult XRAPI_CALL CreateActionSet(XrInstance, const XrActionSetCreateInfo* info, XrActionSet* out)
{
	if (!ValidPath("/" + std::string(info->actionSetName)))
		return XR_ERROR_PATH_FORMAT_INVALID;
	std::lock_guard<std::recursive_mutex> g(St().lock);
	auto set = std::make_unique<RtActionSet>();
	set->name = info->actionSetName;
	RtActionSet* raw = set.get();
	St().actionSets[raw] = std::move(set);
	*out = (XrActionSet)raw;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL DestroyActionSet(XrActionSet set)
{
	std::lock_guard<std::recursive_mutex> g(St().lock);
	for (auto it = St().actions.begin(); it != St().actions.end();) {
		if (it->second->set == (RtActionSet*)set)
			it = St().actions.erase(it);
		else
			++it;
	}
	St().actionSets.erase((RtActionSet*)set);
	return XR_SUCCESS;
}

XrResult XRAPI_CALL CreateAction(XrActionSet set, const XrActionCreateInfo* info, XrAction* out)
{
	std::lock_guard<std::recursive_mutex> g(St().lock);
	auto sit = St().actionSets.find((RtActionSet*)set);
	if (sit == St().actionSets.end())
		return XR_ERROR_HANDLE_INVALID;
	if (sit->second->attached)
		return XR_ERROR_ACTIONSETS_ALREADY_ATTACHED;
	if (!ValidPath("/" + std::string(info->actionName)))
		return XR_ERROR_PATH_FORMAT_INVALID;
	auto action = std::make_unique<RtAction>();
	action->set = sit->second.get();
	action->name = info->actionName;
	action->type = info->actionType;
	for (uint32_t i = 0; i < info->countSubactionPaths; i++)
		action->subactionPaths.push_back(info->subactionPaths[i]);
	RtAction* raw = action.get();
	St().actions[raw] = std::move(action);
	*out = (XrAction)raw;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL DestroyAction(XrAction action)
{
	std::lock_guard<std::recursive_mutex> g(St().lock);
	St().actions.erase((RtAction*)action);
	return XR_SUCCESS;
}

XrResult XRAPI_CALL SuggestInteractionProfileBindings(XrInstance, const XrInteractionProfileSuggestedBinding* info)
{
	XrResult injected;
	if (ScriptFails("xrSuggestInteractionProfileBindings", &injected))
		return injected;
	std::lock_guard<std::recursive_mutex> g(St().lock);
	std::string profile = RtPathString(info->interactionProfile);
	if (profile.rfind("/interaction_profiles/", 0) != 0)
		return XR_ERROR_PATH_UNSUPPORTED;
	std::vector<std::pair<RtAction*, XrPath>> list;
	for (uint32_t i = 0; i < info->countSuggestedBindings; i++) {
		const XrActionSuggestedBinding& b = info->suggestedBindings[i];
		if (!St().actions.count((RtAction*)b.action))
			return XR_ERROR_HANDLE_INVALID;
		std::string path = RtPathString(b.binding);
		if (!ValidPath(path) || path.rfind("/user/", 0) != 0)
			return XR_ERROR_PATH_UNSUPPORTED;
		list.emplace_back((RtAction*)b.action, b.binding);
	}
	St().suggested[info->interactionProfile] = list;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL AttachSessionActionSets(XrSession session, const XrSessionActionSetsAttachInfo* info)
{
	XrResult injected;
	if (ScriptFails("xrAttachSessionActionSets", &injected))
		return injected;
	std::lock_guard<std::recursive_mutex> g(St().lock);
	RtSession* s = Session(session);
	if (!s)
		return XR_ERROR_HANDLE_INVALID;
	if (s->actionsAttached)
		return XR_ERROR_ACTIONSETS_ALREADY_ATTACHED;
	for (uint32_t i = 0; i < info->countActionSets; i++) {
		auto it = St().actionSets.find((RtActionSet*)info->actionSets[i]);
		if (it == St().actionSets.end())
			return XR_ERROR_HANDLE_INVALID;
		it->second->attached = true;
	}
	s->actionsAttached = true;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL SyncActions(XrSession session, const XrActionsSyncInfo* info)
{
	XrResult injected;
	if (ScriptFails("xrSyncActions", &injected))
		return injected;
	std::lock_guard<std::recursive_mutex> g(St().lock);
	RtSession* s = Session(session);
	if (!s)
		return XR_ERROR_HANDLE_INVALID;
	if (!s->actionsAttached)
		return XR_ERROR_ACTIONSET_NOT_ATTACHED;
	s->activeSets.clear();
	for (uint32_t i = 0; i < info->countActiveActionSets; i++)
		s->activeSets.insert((RtActionSet*)info->activeActionSets[i].actionSet);

	if (s->currentProfile == XR_NULL_PATH && !GetScript().interactionProfile.empty()) {
		XrPath profile = RtPath(GetScript().interactionProfile);
		if (St().suggested.count(profile)) {
			s->currentProfile = profile;
			XrEventDataBuffer buf{ XR_TYPE_EVENT_DATA_BUFFER };
			auto* ev = (XrEventDataInteractionProfileChanged*)&buf;
			ev->type = XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED;
			ev->session = session;
			QueueEvent(buf);
		}
	}
	for (const ScriptInput& in : GetScript().inputs)
		if (in.frame <= s->framesWaited)
			s->inputValues[in.path] = in.value; // later entries for a path override earlier ones
	return XR_SUCCESS;
}

bool ActionActive(RtSession* s, RtAction* a)
{
	return s->activeSets.count(a->set) && !BoundPaths(s, a, XR_NULL_PATH).empty();
}

XrResult XRAPI_CALL GetActionStateBoolean(XrSession session, const XrActionStateGetInfo* info, XrActionStateBoolean* state)
{
	std::lock_guard<std::recursive_mutex> g(St().lock);
	RtSession* s = Session(session);
	auto it = St().actions.find((RtAction*)info->action);
	if (!s || it == St().actions.end())
		return XR_ERROR_HANDLE_INVALID;
	RtAction* a = it->second.get();
	if (a->type != XR_ACTION_TYPE_BOOLEAN_INPUT)
		return XR_ERROR_ACTION_TYPE_MISMATCH;
	bool value = false;
	for (const std::string& p : BoundPaths(s, a, info->subactionPath))
		value = value || InputValue(s, p) >= 0.5f;
	state->isActive = ActionActive(s, a) ? XR_TRUE : XR_FALSE;
	state->changedSinceLastSync = XR_FALSE;
	state->currentState = value ? XR_TRUE : XR_FALSE;
	state->lastChangeTime = St().time;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL GetActionStateFloat(XrSession session, const XrActionStateGetInfo* info, XrActionStateFloat* state)
{
	std::lock_guard<std::recursive_mutex> g(St().lock);
	RtSession* s = Session(session);
	auto it = St().actions.find((RtAction*)info->action);
	if (!s || it == St().actions.end())
		return XR_ERROR_HANDLE_INVALID;
	RtAction* a = it->second.get();
	if (a->type != XR_ACTION_TYPE_FLOAT_INPUT)
		return XR_ERROR_ACTION_TYPE_MISMATCH;
	// Several bound inputs: the one of the largest magnitude, as OpenXR 1.0 11.4 has it (a negative axis too)
	float value = 0;
	for (const std::string& p : BoundPaths(s, a, info->subactionPath)) {
		float v = InputValue(s, p);
		if (std::fabs(v) > std::fabs(value))
			value = v;
	}
	state->isActive = ActionActive(s, a) ? XR_TRUE : XR_FALSE;
	state->changedSinceLastSync = XR_FALSE;
	state->currentState = value;
	state->lastChangeTime = St().time;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL GetActionStateVector2f(XrSession session, const XrActionStateGetInfo* info, XrActionStateVector2f* state)
{
	std::lock_guard<std::recursive_mutex> g(St().lock);
	RtSession* s = Session(session);
	auto it = St().actions.find((RtAction*)info->action);
	if (!s || it == St().actions.end())
		return XR_ERROR_HANDLE_INVALID;
	RtAction* a = it->second.get();
	if (a->type != XR_ACTION_TYPE_VECTOR2F_INPUT)
		return XR_ERROR_ACTION_TYPE_MISMATCH;
	XrVector2f value{ 0, 0 };
	for (const std::string& p : BoundPaths(s, a, info->subactionPath)) {
		value.x = InputValue(s, p + "/x");
		value.y = InputValue(s, p + "/y");
	}
	state->isActive = ActionActive(s, a) ? XR_TRUE : XR_FALSE;
	state->changedSinceLastSync = XR_FALSE;
	state->currentState = value;
	state->lastChangeTime = St().time;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL GetActionStatePose(XrSession session, const XrActionStateGetInfo* info, XrActionStatePose* state)
{
	std::lock_guard<std::recursive_mutex> g(St().lock);
	RtSession* s = Session(session);
	auto it = St().actions.find((RtAction*)info->action);
	if (!s || it == St().actions.end())
		return XR_ERROR_HANDLE_INVALID;
	state->isActive = ActionActive(s, it->second.get()) ? XR_TRUE : XR_FALSE;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL CreateActionSpace(XrSession, const XrActionSpaceCreateInfo* info, XrSpace* out)
{
	std::lock_guard<std::recursive_mutex> g(St().lock);
	auto it = St().actions.find((RtAction*)info->action);
	if (it == St().actions.end())
		return XR_ERROR_HANDLE_INVALID;
	auto sp = std::make_unique<RtSpace>();
	sp->isAction = true;
	sp->offset = info->poseInActionSpace;
	sp->action = it->second.get();
	sp->subaction = info->subactionPath;
	int hand = HandOf(RtPathString(info->subactionPath));
	sp->actionPose = HandPose(hand < 0 ? 1 : hand);
	RtSpace* raw = sp.get();
	St().spaces[raw] = std::move(sp);
	*out = (XrSpace)raw;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL ApplyHapticFeedback(XrSession session, const XrHapticActionInfo* info, const XrHapticBaseHeader* haptic)
{
	std::lock_guard<std::recursive_mutex> g(St().lock);
	RtSession* s = Session(session);
	if (!s)
		return XR_ERROR_HANDLE_INVALID;
	if (haptic->type == XR_TYPE_HAPTIC_VIBRATION) {
		auto* v = (const XrHapticVibration*)haptic;
		Json::Value entry;
		entry["frame"] = (Json::UInt64)s->framesWaited;
		entry["path"] = RtPathString(info->subactionPath);
		entry["amplitude"] = v->amplitude;
		entry["duration_ns"] = (Json::Int64)v->duration;
		entry["frequency"] = v->frequency;
		Dump& d = GetDump();
		std::lock_guard<std::mutex> dg(d.lock);
		// The first 1000, then only a count: a game (or a test) calling it in a loop mustn't fill the memory
		if (d.root["haptics"].size() < 1000)
			d.root["haptics"].append(entry);
		d.root["haptics_total"] = d.root["haptics_total"].asUInt64() + 1;
	}
	return XR_SUCCESS;
}

XrResult XRAPI_CALL StopHapticFeedback(XrSession, const XrHapticActionInfo*)
{
	return XR_SUCCESS;
}

XrResult XRAPI_CALL GetCurrentInteractionProfile(XrSession session, XrPath topLevel, XrInteractionProfileState* state)
{
	std::lock_guard<std::recursive_mutex> g(St().lock);
	RtSession* s = Session(session);
	if (!s)
		return XR_ERROR_HANDLE_INVALID;
	if (!s->actionsAttached)
		return XR_ERROR_ACTIONSET_NOT_ATTACHED;
	std::string top = RtPathString(topLevel);
	bool hand = top == "/user/hand/left" || top == "/user/hand/right";
	state->interactionProfile = hand ? s->currentProfile : XR_NULL_PATH;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL EnumerateBoundSourcesForAction(XrSession session, const XrBoundSourcesForActionEnumerateInfo* info, uint32_t capacity, uint32_t* count, XrPath* sources)
{
	std::lock_guard<std::recursive_mutex> g(St().lock);
	RtSession* s = Session(session);
	auto it = St().actions.find((RtAction*)info->action);
	if (!s || it == St().actions.end())
		return XR_ERROR_HANDLE_INVALID;
	std::vector<XrPath> list;
	for (const std::string& p : BoundPaths(s, it->second.get(), XR_NULL_PATH))
		list.push_back(RtPath(p));
	*count = (uint32_t)list.size();
	if (capacity == 0)
		return XR_SUCCESS;
	if (capacity < list.size())
		return XR_ERROR_SIZE_INSUFFICIENT;
	for (size_t i = 0; i < list.size(); i++)
		sources[i] = list[i];
	return XR_SUCCESS;
}

XrResult XRAPI_CALL GetInputSourceLocalizedName(XrSession, const XrInputSourceLocalizedNameGetInfo* info, uint32_t capacity, uint32_t* count, char* buffer)
{
	std::string name = RtPathString(info->sourcePath);
	*count = (uint32_t)name.size() + 1;
	if (capacity == 0)
		return XR_SUCCESS;
	if (capacity < *count)
		return XR_ERROR_SIZE_INSUFFICIENT;
	std::memcpy(buffer, name.c_str(), *count);
	return XR_SUCCESS;
}

} // namespace

PFN_xrVoidFunction RtActionsLookup(const char* name)
{
	struct Entry {
		const char* name;
		PFN_xrVoidFunction fn;
	};
	static const Entry entries[] = {
		{ "xrCreateActionSet", (PFN_xrVoidFunction)CreateActionSet },
		{ "xrDestroyActionSet", (PFN_xrVoidFunction)DestroyActionSet },
		{ "xrCreateAction", (PFN_xrVoidFunction)CreateAction },
		{ "xrDestroyAction", (PFN_xrVoidFunction)DestroyAction },
		{ "xrSuggestInteractionProfileBindings", (PFN_xrVoidFunction)SuggestInteractionProfileBindings },
		{ "xrAttachSessionActionSets", (PFN_xrVoidFunction)AttachSessionActionSets },
		{ "xrSyncActions", (PFN_xrVoidFunction)SyncActions },
		{ "xrGetActionStateBoolean", (PFN_xrVoidFunction)GetActionStateBoolean },
		{ "xrGetActionStateFloat", (PFN_xrVoidFunction)GetActionStateFloat },
		{ "xrGetActionStateVector2f", (PFN_xrVoidFunction)GetActionStateVector2f },
		{ "xrGetActionStatePose", (PFN_xrVoidFunction)GetActionStatePose },
		{ "xrCreateActionSpace", (PFN_xrVoidFunction)CreateActionSpace },
		{ "xrApplyHapticFeedback", (PFN_xrVoidFunction)ApplyHapticFeedback },
		{ "xrStopHapticFeedback", (PFN_xrVoidFunction)StopHapticFeedback },
		{ "xrGetCurrentInteractionProfile", (PFN_xrVoidFunction)GetCurrentInteractionProfile },
		{ "xrEnumerateBoundSourcesForAction", (PFN_xrVoidFunction)EnumerateBoundSourcesForAction },
		{ "xrGetInputSourceLocalizedName", (PFN_xrVoidFunction)GetInputSourceLocalizedName },
	};
	for (const Entry& e : entries)
		if (!std::strcmp(e.name, name))
			return e.fn;
	return nullptr;
}

void RtActionsOnFrame(RtSession* s)
{
	if (!RtExtensionEnabled("XR_EXT_user_presence"))
		return;
	for (const ScriptPresence& p : GetScript().presence) {
		if (p.frame != s->framesWaited)
			continue;
		// XrEventDataUserPresenceChangedEXT (not in the bundled 1.0.12 headers): type, next, session, isUserPresent
		XrEventDataBuffer buf{ XR_TYPE_EVENT_DATA_BUFFER };
		struct UserPresence {
			XrStructureType type;
			const void* next;
			XrSession session;
			XrBool32 isUserPresent;
		};
		auto* ev = (UserPresence*)&buf;
		ev->type = (XrStructureType)1000470000; // XR_TYPE_EVENT_DATA_USER_PRESENCE_CHANGED_EXT
		ev->next = nullptr;
		ev->session = (XrSession)s;
		ev->isUserPresent = p.worn ? XR_TRUE : XR_FALSE;
		QueueEvent(buf);
	}
}

bool RtActionSpaceScripted(const RtSpace* sp, uint64_t frame, ScriptPose* out)
{
	if (!sp->action || GetScript().poses.empty() || !St().actions.count(sp->action))
		return false;
	std::vector<std::string> paths;
	if (RtSession* s = St().session.get())
		paths = BoundPaths(s, sp->action, sp->subaction);
	if (paths.empty()) {
		// No current profile yet (or nothing bound in it): any profile's suggestion for the action
		std::string prefix = sp->subaction != XR_NULL_PATH ? RtPathString(sp->subaction) : "";
		for (auto& [profile, list] : St().suggested)
			for (auto& [a, path] : list) {
				std::string p = RtPathString(path);
				if (a == sp->action && (prefix.empty() || p.rfind(prefix, 0) == 0))
					paths.push_back(p);
			}
	}
	for (const std::string& p : paths) {
		if (const ScriptPose* pose = ScriptPoseAt(p, frame)) {
			*out = *pose;
			return true;
		}
	}
	return false;
}

XrPosef RtSpaceInStage(const RtSpace* sp, ScriptPose* velocity)
{
	if (velocity)
		*velocity = ScriptPose{};
	XrPosef base = sp->isAction ? sp->actionPose : (sp->type == XR_REFERENCE_SPACE_TYPE_VIEW ? kHead : kIdentity);
	ScriptPose scripted;
	uint64_t frame = St().session ? St().session->framesWaited : 0;
	if (sp->isAction && RtActionSpaceScripted(sp, frame, &scripted)) {
		base = scripted.pose;
		if (velocity)
			*velocity = scripted;
	}
	return Compose(base, sp->offset);
}

void RtActionsReset()
{
	St().actions.clear();
	St().actionSets.clear();
	St().suggested.clear();
}

void RtActionsDump(Json::Value& root)
{
	root["action_calls_without_session"] = (Json::UInt64)callsWithoutSession.load();
	root["actions"] = Json::Value(Json::arrayValue);
	for (auto& [p, a] : St().actions) {
		Json::Value entry;
		entry["set"] = a->set ? a->set->name : "";
		entry["name"] = a->name;
		entry["type"] = (int)a->type;
		root["actions"].append(entry);
	}
	root["suggested_bindings"] = Json::Value(Json::objectValue);
	// The same with each binding's action and its type (XrActionType): [{ "action", "type", "path" }]
	root["suggested_actions"] = Json::Value(Json::objectValue);
	for (auto& [profile, list] : St().suggested) {
		Json::Value paths(Json::arrayValue), withActions(Json::arrayValue);
		for (auto& [action, path] : list) {
			paths.append(RtPathString(path));
			// An action destroyed since (with its set) is left with no name
			auto live = St().actions.find(action);
			Json::Value entry;
			entry["action"] = live != St().actions.end() ? live->second->name : "";
			entry["type"] = live != St().actions.end() ? (int)live->second->type : 0;
			entry["path"] = RtPathString(path);
			withActions.append(entry);
		}
		root["suggested_bindings"][RtPathString(profile)] = paths;
		root["suggested_actions"][RtPathString(profile)] = withActions;
	}
}
