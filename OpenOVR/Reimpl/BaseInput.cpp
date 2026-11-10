#include "generated/interfaces/vrtypes.h"
#include "logging.h"
#include "openxr/openxr.h"
#include "json/json.h"
#define BASE_IMPL
#include "BaseInput.h"
#include "Misc/AppManifest.h"
#include "Misc/Input/Bindings/Conversion.h"
#include "Misc/ProgramEvents.h"
#include "Misc/Config.h"
#include <string>

#include <convert.h>

#include "Drivers/Backend.h"
#include "generated/static_bases.gen.h"
#include <algorithm>
#include <cmath>
#include <codecvt>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <filesystem>
#include <glm/gtc/matrix_inverse.hpp>
#include <locale>
#include <map>
#include <mutex>
#include <unordered_set>
#include <math.h>
#include <numbers>
#include <optional>
#include <set>
#include <utility>

#include "Misc/JsonFile.h"
#include "Misc/xrmoreutils.h"

// Use RenderModels for the pose offsets, which are the same as component positions
#include "BaseRenderModels.h"

using namespace vr;

#include "../DrvOpenXR/XrBackend.h"

// On Android, the application must supply a function to load the contents of a file

/**
 * Macro for creating an Action object from a handle and verifying isn't invalid.
 * If it is invalid, it will cause the surrounding function to return VRInputError_InvalidHandle.
 * action_var_name is the name of the resulting Action object if the handle isn't invalid.
 */
#define GET_ACTION_FROM_HANDLE(action_var_name, handle)              \
	Action* action_var_name = cast_AH(handle);                       \
	do {                                                             \
		if (!action_var_name) {                                      \
			OOVR_LOG_ONCE("WARNING: Invalid action handle passed!"); \
			return VRInputError_InvalidHandle;                       \
		}                                                            \
	} while (0)

/**
 * Same as GET_ACTION_FROM_HANDLE but for ActionSets.
 */
#define GET_ACTION_SET_FROM_HANDLE(action_var_name, handle)          \
	ActionSet* action_var_name = cast_ASH(handle);                   \
	do {                                                             \
		if (!action_var_name) {                                      \
			OOVR_LOG_ONCE("WARNING: Invalid action handle passed!"); \
			return VRInputError_InvalidHandle;                       \
		}                                                            \
	} while (0)

// This is a duplicate from BaseClientCore.cpp
// Manifests and binding files as SteamVR reads them (BOM, comments, trailing commas: Misc/JsonFile.h); false with
// the reason in error if the file can't be read or isn't JSON
// path is UTF-8, as OpenVR passes it; one that isn't (an older game's ANSI path) is tried as the system code page on
// Windows, and is an error otherwise, never an exception
static bool ReadJson(const std::string& path, Json::Value& result, std::string& error)
{
#ifndef ANDROID
	try {
		std::u8string utf8(path.begin(), path.end());
		return ReadJsonFileLenient(std::filesystem::path(utf8), result, error);
	} catch (const std::exception& e) {
#ifdef _WIN32
		try {
			return ReadJsonFileLenient(std::filesystem::path(path), result, error);
		} catch (const std::exception&) {
		}
#endif
		error = std::string("can't use the path ") + path + ": " + e.what();
		result = Json::Value();
		return false;
	}
#else
	std::string contents = OpenComposite_Android_Load_Input_File(path.c_str());
	return ParseJsonLenient(contents, result, error);
#endif
}

// A binding with no action or no path (OpenXR couldn't make one, logged) is left out: one bad entry would make
// xrSuggestInteractionProfileBindings refuse the whole controller's bindings
static void AddSuggestedBinding(std::vector<XrActionSuggestedBinding>& bindings, XrAction action, XrPath path)
{
	if (action == XR_NULL_HANDLE || path == XR_NULL_PATH)
		return;
	bindings.push_back(XrActionSuggestedBinding{ action, path });
}

static std::string dirnameOf(const std::string& fname)
{
	size_t pos = fname.find_last_of("\\/");
	return (std::string::npos == pos)
	    ? ""
	    : fname.substr(0, pos);
}

// ASCII-only to-lower-case - for use with strings in maps, since steamvr is case-independent (In which
// locale you might wonder? Good question).
static std::string lowerStr(const std::string& in)
{
	std::string out;
	out.reserve(in.size());

	for (char c : in) {
		if (c >= 'A' && c <= 'Z') {
			c = (char)(c - 'A' + 'a');
		}
		out.push_back(c);
	}

	return out;
}

static void stringSplit(const std::string& str, std::vector<std::string>& items)
{
	items.clear();

	for (size_t i = 0; i < str.size();) {
		size_t nextStroke = str.find('/', i);

		// If the path starts with a stroke, ignore it - that's a very common case
		if (nextStroke == 0) {
			i++;
			continue;
		}

		// No more strokes in the string? This is the last bit
		if (nextStroke == std::string::npos) {
			items.push_back(str.substr(i));
			break;
		}

		items.push_back(str.substr(i, nextStroke - i));

		// Skip to one past the next stroke
		i = nextStroke + 1;
	}
}

// Converts an arbitrary application-supplied string to one we can use with OpenXR
static std::string escapePathString(const std::string& str)
{
	std::vector<char> out;
	out.reserve(str.length() + 10);
	for (char c : str) {
		// Valid list of characters, see the OpenXR spec part 6.2
		if (('a' <= c && c <= 'z') || ('0' <= c && c <= '9') || c == '-' || c == '_' || c == '.' || c == '/') {
			out.push_back(c);
			continue;
		}

		// Special-case uppercase letters by prefixing them with an underscore and lowering them
		// This will nicely convert CamelCase to snake_case, though THIS_CASE becomes ugly.
		if ('A' <= c && c <= 'Z') {
			out.push_back('_');
			out.push_back(c - 'A' + 'a');
			continue;
		}

		// Just print out the hex code
		char hex[3];
		snprintf(hex, sizeof(hex), "%02x", c);
		out.push_back('.');
		out.push_back(hex[0]);
		out.push_back(hex[1]);
	}

	return std::string(out.data(), out.size());
}

static std::string pathFromParts(const std::initializer_list<std::string>& parts)
{
	std::string str;
	for (const std::string& part : parts) {
		str += "/";
		str += part;
	}
	return str;
}

// Must be defined non-inline to avoid it ending up in stubs.gen.cpp
BaseInput::Action::~Action() = default;
BaseInput::ActionSource::~ActionSource() = default;
// The input value handles GetInputSourceHandle has handed out (they're pointers): cast_IVH answers null for any
// other value instead of reading through it. Never freed: ~InputValueHandle can run during static destruction (the
// interfaces map in openvr_api.cpp keeps BaseInput alive until then)
static std::mutex& LiveHandlesLock()
{
	static std::mutex& lock = *new std::mutex;
	return lock;
}
static std::unordered_set<const void*>& LiveHandles()
{
	static std::unordered_set<const void*>& handles = *new std::unordered_set<const void*>;
	return handles;
}

BaseInput::InputValueHandle::InputValueHandle()
{
	std::lock_guard<std::mutex> guard(LiveHandlesLock());
	LiveHandles().insert(this);
}
BaseInput::InputValueHandle::~InputValueHandle()
{
	std::lock_guard<std::mutex> guard(LiveHandlesLock());
	LiveHandles().erase(this);
}

// Registry implementation

template <typename T>
BaseInput::Registry<T>::Registry(uint32_t _maxNameSize)
    : maxNameSize(_maxNameSize) {}

template <typename T>
T* BaseInput::Registry<T>::LookupItem(const std::string& name) const
{
	auto iter = itemsByName.find(lowerStr(name));
	if (iter == itemsByName.end())
		return nullptr;
	return iter->second;
}

template <typename T>
T* BaseInput::Registry<T>::LookupItem(RegHandle handle) const
{
	auto iter = itemsByHandle.find(handle);
	if (iter == itemsByHandle.end())
		return nullptr;
	return iter->second;
}

template <typename T>
BaseInput::RegHandle BaseInput::Registry<T>::LookupHandle(const std::string& name)
{
	std::string lowerName = ShortenOrLookupName(name);
	auto iter = handlesByName.find(lowerName);
	if (iter != handlesByName.end())
		return iter->second;

	// Create a new dummy handle. It's only in the handlesByName map, so looking up the item
	// for it will return nullptr.
	RegHandle handle = 0xabcd0001 + handlesByName.size();
	handlesByName[lowerName] = handle;
	namesByHandle[handle] = name;
	return handle;
}

template <typename T>
std::string BaseInput::Registry<T>::ShortenOrLookupName(const std::string& longName)
{
	std::string ret = lowerStr(longName);
	if (ret.size() > maxNameSize - 1) {
		auto iter = longNames.find(ret);
		if (iter == longNames.end()) {
			// new name - shorten and append "_ln" + unique number (ln for Long Name)
			std::string fullName = ret;
			std::string unique_id = "_ln" + std::to_string(longNames.size());
			ret = ret.substr(0, maxNameSize - 1 - unique_id.size()) + unique_id;
			longNames[fullName] = ret;
			OOVR_LOGF("Shortened name %s to %s", fullName.c_str(), ret.c_str());
		} else {
			// name has already been shortened before - return the shortened version. It may have no handle yet, if
			// nothing was made with it: an action the manifest parser skips, or a binding's action not in the manifest.
			return iter->second;
		}
	}
	return ret;
}

template <typename T>
T* BaseInput::Registry<T>::Initialise(const std::string& name, std::unique_ptr<T> value)
{
	std::string lowerName = lowerStr(name);
	RegHandle handle = 0;

	// apparently games CAN in fact grab handles before initialization - Kayak VR does this
	// grab already created handle if it exists
	if (handlesByName.count(lowerName) != 0) {
		// since we only generate dummy handles before initialization, make sure we only have dummy handles
		// dummy handles have no associated items
		handle = handlesByName.find(lowerName)->second;
		if (itemsByHandle.find(handle) != itemsByHandle.end()) {
			// Can't happen (an item is only made once per name); if it does, the newer item replaces the older
			OOVR_LOGF("Input registry: '%s' initialised twice", lowerName.c_str());
		}
	}

	// Move the pointer into storage, so we'll own it
	T* ptr = value.get();
	storage.emplace_back(std::move(value));

	// Use pointer as handle, for ease of debugging only
	// Since our values live as long as the registry, it's guaranteed their addresses won't repeat
	if (!handle) {
		handle = (RegHandle)ptr;
		handlesByName[lowerName] = handle;
		namesByHandle[handle] = lowerName;
	}

	itemsByName[lowerName] = ptr;
	itemsByHandle[handle] = ptr;

	// Convenience return
	return ptr;
}

template <typename T>
void BaseInput::Registry<T>::Reset()
{
	// We want to preserve handlesByName and namesByHandle, because handles are supposed to always be accessible
	// from the same values regardless of if said handles are actually currently valid
	// In the case of NomaiVR, it will set an action manifest, get all the action handles, and then set another (identical) manifest
	// We can clear the actual item storage though, since these will no longer be valid
	itemsByHandle.clear();
	itemsByName.clear();
	storage.clear();
}

// ---

const BaseInput::ActionPerProfileData BaseInput::Action::defaultActionData;

BaseInput::BaseInput()
    : actionSets(XR_MAX_ACTION_SET_NAME_SIZE), actions(XR_MAX_ACTION_NAME_SIZE)
{
	// Initialise the subaction path constants
	for (const std::string& str : allSubactionPathNames) {
		XrPath path = XR_NULL_PATH;
		OC_XR_LOG(xrStringToPath(xr_instance, str.c_str(), &path));
		allSubactionPaths.push_back(path);
		handInteractionProfiles.push_back(nullptr);
	}

	if (!startupManifest.empty()) {
		SetActionManifestPath(startupManifest.c_str());
	}
}
BaseInput::~BaseInput()
{
	for (XrHandTrackerEXT& handTracker : handTrackers) {
		if (handTracker != XR_NULL_HANDLE)
			xr_ext->xrDestroyHandTrackerEXT(handTracker);
		handTracker = XR_NULL_HANDLE;
	}
}

void BaseInput::setStartupManifest(std::string manifest)
{
	startupManifest = manifest;
	if (auto input = GetUnsafeBaseInput(); input) {
		input->SetActionManifestPath(startupManifest.c_str());
	}
}

EVRInputError BaseInput::SetActionManifestPath(const char* pchActionManifestPath)
{
	if (!pchActionManifestPath || !*pchActionManifestPath)
		return VRInputError_InvalidParam;
	OOVR_LOGF("Loading manifest file '%s'", pchActionManifestPath);

	//////////////
	//// Load the actions from the manifest file
	//////////////

	if (hasLoadedActions && loadedActionsPath == pchActionManifestPath)
		return vr::VRInputError_None; // It stands to reason that nothing would happen if we try to load the same manifest again

	// Read it first: a manifest that can't be read leaves the game on legacy input (and any manifest it had), and
	// answers InvalidParam as SteamVR does for a path it can't use
	Json::Value root;
	std::string manifestError;
	if (!ReadJson(pchActionManifestPath, root, manifestError) || !root.isObject()) {
		OOVR_LOGF("Failed to open or parse input manifest: %s", manifestError.empty() ? "not a JSON object" : manifestError.c_str());
		return VRInputError_InvalidParam;
	}

	bool reloading = hasLoadedActions;
	if (hasLoadedActions) {
		// It stands to reason that nothing would happen if we try to load the same manifest again
		if (loadedActionsPath == pchActionManifestPath)
			return vr::VRInputError_None;

		OOVR_LOG("Received another manifest! Restarting session to reattach inputs...");
		for (std::unique_ptr<ActionSet>& as : actionSets.GetItems()) {
			OC_XR_LOG(xrDestroyActionSet(as->xr));
		}
		OC_XR_LOG(xrDestroyActionSet(legacyInputsSet));
		legacyInputsSet = XR_NULL_HANDLE;
		actions.Reset();
		actionSets.Reset();
		DpadBindingInfo::parents.clear();
		indexGripExtensionActions.clear();
		usingLegacyInput = false;
	}

	restartingSession = true;
	XrBackend::MaybeRestartForInputs();
	restartingSession = false;

	hasLoadedActions = true;
	loadedActionsPath = pchActionManifestPath;

	// Another manifest in place of the first: its bindings are new
	if (reloading) {
		OcPostEvent(vr::VREvent_ActionBindingReloaded, vr::k_unTrackedDeviceIndexInvalid, vr::VREvent_Data_t{}, false);
	}

	// Random setting which is in the action manifest
	const Json::Value& dominantHandSetting = root["supports_dominant_hand_setting"];
	allowSetDominantHand = dominantHandSetting.isConvertibleTo(Json::booleanValue) && dominantHandSetting.asBool();

	// Parse the actions
	OOVR_LOG("Parsing actions...");
	std::set<std::string> skippedActionSets; // the sets of actions we leave out, which the app still syncs
	for (Json::Value item : root["actions"]) try {
		if (!item.isObject()) {
			OOVR_LOG("Warning: skipping an action that isn't a JSON object");
			continue;
		}
		std::unique_ptr<Action> actionPtr = std::make_unique<Action>();
		Action& action = *actionPtr;

		action.fullName = actions.ShortenOrLookupName(item["name"].asString());

		// Split the full name by stroke ('/') characters
		// They have four parts: the first is always 'actions', the second is the action set name, the third is
		// either 'in' or 'out' (in for inputs, out for haptics) and the last is the action's short name.
		std::vector<std::string> parts;
		stringSplit(action.fullName, parts);

		// A bad entry is skipped with a warning; the rest of the manifest is used
		if (parts.size() != 4) {
			OOVR_LOGF("Warning: skipping action '%s' - wrong number of parts %zu", action.fullName.c_str(), parts.size());
			continue;
		}

		if (parts.at(0) != "actions") {
			OOVR_LOGF("Warning: skipping action '%s' - bad first part '%s'", action.fullName.c_str(), parts.at(0).c_str());
			continue;
		}

		action.setName = parts.at(1);
		action.shortName = parts.at(3);

		std::string inOut = parts.at(2);
		if (inOut == "in")
			action.haptic = false;
		else if (inOut == "out")
			action.haptic = true;
		else {
			OOVR_LOGF("Warning: skipping action '%s' - bad in/out value '%s'", action.fullName.c_str(), inOut.c_str());
			continue;
		}

		// Parse the requirement
		// TODO default to optional
		std::string requirement = item["requirement"].asString();
		if (requirement == "mandatory")
			action.requirement = ActionRequirement::Mandatory;
		else if (requirement == "suggested" || requirement.empty()) // Default
			action.requirement = ActionRequirement::Suggested;
		else if (requirement == "optional")
			action.requirement = ActionRequirement::Optional;
		else {
			OOVR_LOGF("Warning: invalid requirement '%s' for action '%s', using suggested", requirement.c_str(), action.fullName.c_str());
			action.requirement = ActionRequirement::Suggested;
		}

		// Parse the type
		std::string type = item["type"].asString();
		if (type == "boolean")
			action.type = ActionType::Boolean;
		else if (type == "vector1")
			action.type = ActionType::Vector1;
		else if (type == "vector2")
			action.type = ActionType::Vector2;
		else if (type == "vector3") {
			// Not yet: the action is left out (its handle still works and reads as inactive)
			OOVR_LOGF("Warning: skipping vector3 action '%s' (not supported yet)", action.fullName.c_str());
			skippedActionSets.insert(action.setName);
			continue;
		}
		else if (type == "vibration")
			action.type = ActionType::Vibration;
		else if (type == "pose")
			action.type = ActionType::Pose;
		else if (type == "skeleton")
			action.type = ActionType::Skeleton;
		else if (type == "eyetracking") {
			// OpenVR 2.15's eye tracking: there's no gaze to give (GetEyeTrackingData* report none), so leave the
			// action out. GetActionHandle still answers it; reading it answers InvalidHandle with the data zeroed (inactive),
			// and bindings to it are skipped, as for any action not in the manifest.
			OOVR_LOGF("Skipping eye tracking action '%s'", action.fullName.c_str());
			skippedActionSets.insert(action.setName);
			continue;
		} else {
			OOVR_LOGF("Warning: skipping action '%s' of unknown type '%s'", action.fullName.c_str(), type.c_str());
			skippedActionSets.insert(action.setName);
			continue;
		}

		if (actions.LookupItem(action.fullName) != nullptr) {
			OOVR_LOGF("Warning: skipping duplicate action '%s'", action.fullName.c_str());
			continue;
		}

		// If this is a skeletal action, see what hand it's bound to. Uniquely, this is done in the actions manifest itself
		if (action.type == ActionType::Skeleton) {
			// Default to the left hand, so we can always safely use it as a 0 or 1 value for indexing arrays etc
			action.skeletalHand = ITrackedDevice::HAND_LEFT;

			std::string skelSide = item["skeleton"].asString();
			if (skelSide.empty()) {
				OOVR_SOFT_ABORTF("Skeletal hand side not set for action '%s'", action.fullName.c_str());
			} else if (skelSide == "/skeleton/hand/left") {
				action.skeletalHand = ITrackedDevice::HAND_LEFT;
			} else if (skelSide == "/skeleton/hand/right") {
				action.skeletalHand = ITrackedDevice::HAND_RIGHT;
			} else {
				OOVR_SOFT_ABORTF("Unknown/unsupported skeletal hand side '%s' for action '%s'",
				    skelSide.c_str(), action.fullName.c_str());
			}
		}

		std::string name = action.fullName; // Array index may be evaluated first iirc, so pull this out now
		actions.Initialise(name, std::move(actionPtr));
	} catch (const std::exception& e) {
		// A wrong-typed value in this entry (Json::LogicError): only the entry is left out
		OOVR_SOFT_ABORTF("Skipping an action of the manifest: %s", e.what());
	}

	// Parse the action sets
	OOVR_LOG("Parsing action sets...");
	for (Json::Value item : root["action_sets"]) try {
		if (!item.isObject()) {
			OOVR_LOG("Warning: skipping an action set that isn't a JSON object");
			continue;
		}
		ActionSet set = {};

		set.fullName = actionSets.ShortenOrLookupName(item["name"].asString());

		// Split the full name by stroke ('/') characters
		// They have four parts: the first is always 'actions', the second is the action set name, the third is
		// either 'in' or 'out' (in for inputs, out for haptics) and the last is the action's short name.
		std::vector<std::string> parts;
		stringSplit(set.fullName, parts);

		if (parts.size() != 2) {
			OOVR_LOGF("Warning: skipping action set '%s' - wrong number of parts %zu", set.fullName.c_str(), parts.size());
			continue;
		}

		if (parts.at(0) != "actions") {
			OOVR_LOGF("Warning: skipping action set '%s' - bad first part '%s'", set.fullName.c_str(), parts.at(0).c_str());
			continue;
		}
		if (actionSets.LookupItem(set.fullName) != nullptr) {
			OOVR_LOGF("Warning: skipping duplicate action set '%s'", set.fullName.c_str());
			continue;
		}

		set.name = parts.at(1);

		// Find the usage
		std::string usage = item["usage"].asString();
		if (usage == "leftright")
			set.usage = ActionSetUsage::LeftRight;
		else if (usage == "single")
			set.usage = ActionSetUsage::Single;
		else if (usage == "hidden")
			set.usage = ActionSetUsage::Hidden;
		else {
			OOVR_LOGF("Warning: invalid usage '%s' for action set '%s', using leftright", usage.c_str(), set.name.c_str());
			set.usage = ActionSetUsage::LeftRight;
		}

		// Register it
		actionSets.Initialise(set.fullName, std::make_unique<ActionSet>(set));
	} catch (const std::exception& e) {
		OOVR_SOFT_ABORTF("Skipping an action set of the manifest: %s", e.what());
	}

	// Make sure all the actions have a corresponding set
	// In the OpenVR samples, they don't declare their action set. Are they automatically created?!
	// Also Vivecraft unfortunately does this, so we do have to support it.
	auto findOrCreateSet = [&](const std::string& setName, const std::string& forAction) {
		std::string fullName = "/actions/" + setName;
		ActionSet* set = actionSets.LookupItem(fullName);
		if (set == nullptr) {
			OOVR_LOGF("Invalid action set '%s' for action '%s', creating implicit set", setName.c_str(), forAction.c_str());

			// Create this set
			set = actionSets.Initialise(fullName, std::make_unique<ActionSet>());
			set->name = setName;
			set->fullName = fullName;
			set->usage = ActionSetUsage::LeftRight; // Just assume these default sets are in leftright mode, FIXME validate against steamvr
		}
		return set;
	};
	for (const std::unique_ptr<Action>& action : actions.GetItems()) {
		action->set = findOrCreateSet(action->setName, action->fullName);
	}
	for (const std::string& setName : skippedActionSets) {
		findOrCreateSet(setName, "(skipped)");
	}

	//////////////////////////
	/// Now we've got everything done, load the actions into OpenXR
	//////////////////////////

	for (const std::unique_ptr<ActionSet>& as : actionSets.GetItems()) {
		XrActionSetCreateInfo createInfo = { XR_TYPE_ACTION_SET_CREATE_INFO };
		std::string safeName = escapePathString(as->name);
		strcpy_arr(createInfo.actionSetName, safeName.c_str());
		strcpy_arr(createInfo.localizedActionSetName, as->name.c_str()); // TODO localisation

		OC_XR_LOG(xrCreateActionSet(xr_instance, &createInfo, &as->xr));
	}

	for (const std::unique_ptr<Action>& act : actions.GetItems()) {
		XrActionCreateInfo info = { XR_TYPE_ACTION_CREATE_INFO };
		std::string safeName = escapePathString(act->shortName);
		strcpy_arr(info.actionName, safeName.c_str());
		strcpy_arr(info.localizedActionName, act->shortName.c_str()); // TODO localisation

		switch (act->type) {
		case ActionType::Boolean:
			info.actionType = XR_ACTION_TYPE_FLOAT_INPUT; // do the float->boolean conversion in here
			break;
		case ActionType::Vector1:
			info.actionType = XR_ACTION_TYPE_FLOAT_INPUT;
			break;
		case ActionType::Vector2:
			info.actionType = XR_ACTION_TYPE_VECTOR2F_INPUT;
			break;
		case ActionType::Vector3:
			// Skipped while parsing (not supported yet)
			continue;
		case ActionType::Vibration:
			info.actionType = XR_ACTION_TYPE_VIBRATION_OUTPUT;
			break;
		case ActionType::Pose:
			// Poses are special, since they have offsets
			continue;
		case ActionType::Skeleton:
			// Don't actually create an action for skeletons, since we'll get their data from the hand tracking extension.
			continue;
		default:
			OOVR_SOFT_ABORTF("Bad action type while remapping action %s: %d", act->fullName.c_str(), static_cast<int>(act->type));
			continue;
		}

		// Listen on all the subactions
		info.subactionPaths = allSubactionPaths.data();
		info.countSubactionPaths = allSubactionPaths.size();

		OC_XR_LOG(xrCreateAction(act->set->xr, &info, &act->xr));
	}

	CreateLegacyActions();

	// load all available bindings for known interaction profiles
	OOVR_LOG("Loading known bindings...");
	// Map of OpenVR names (i.e. "vive_controller") to interaction profile pointers
	std::unordered_map<std::string, InteractionProfile*> OpenVRNameToProfile;
	for (const std::unique_ptr<InteractionProfile>& profile : InteractionProfile::GetProfileList()) {
		if (profile->GetOpenVRName().has_value())
			OpenVRNameToProfile[profile->GetOpenVRName().value()] = profile.get();
		else
			// Add any unrecognized profiles (simple controller) to the end so it still gets assigned a profile
			OpenVRNameToProfile[profile->GetPath()] = profile.get();
	}

	// For determining the best path for supported controllers that don't have a profile handled by the game (i.e. khr/simple_controller)
	std::string backupPath;
	int8_t priority = -1;

	// these priorities are a bit arbitrary
	const std::unordered_map<std::string, uint8_t> typeToPriority = {
		{ "oculus_touch", 3 },
		{ "knuckles", 2 },
		{ "generic", 1 }
	};

	for (Json::Value item : root["default_bindings"]) {
		std::string controller_type = item["controller_type"].asString();
		std::string path = dirnameOf(pchActionManifestPath) + "/" + item["binding_url"].asString();

		// check if a custom binding file exists in current_dir/OpenComposite/controller_type.json
		std::filesystem::path customPath = std::filesystem::current_path() / "OpenComposite" / (controller_type + ".json");

		if (std::filesystem::exists(customPath)) {
			OOVR_LOGF("Loading user-defined custom bindings file for controller type %s", controller_type.c_str());
			path = customPath.string();
		}

		// look if we know about this OpenVR name
		// have to loop through every binding type because default_bindings is an array for some reason
		auto iter = OpenVRNameToProfile.find(controller_type);
		if (iter != OpenVRNameToProfile.end()) {
			LoadBindingsSet(*(iter->second), path);
			// profile has been bound: we don't need it in our map anymore
			OpenVRNameToProfile.erase(iter);
		}

		// get priority for remaining unbound profiles
		auto iter2 = typeToPriority.find(controller_type);
		if (iter2 != typeToPriority.end()) {
			if (iter2->second > priority) {
				priority = iter2->second;
				backupPath = path;
			}
		} else if (priority < 0) {
			priority = 0;
			backupPath = path;
		}
	}

	// remaining profiles: bind to our backup path
	for (auto& [name, profile_ptr] : OpenVRNameToProfile) {
		LoadBindingsSet(*profile_ptr, backupPath);
	}

	// Attach everything to the current session
	BindInputsForSession();
	return vr::VRInputError_None;
}

void BaseInput::LoadEmptyManifestIfRequired()
{
	if (hasLoadedActions)
		return;

	restartingSession = true;
	XrBackend::MaybeRestartForInputs();
	restartingSession = false;

	OOVR_LOG("Loading virtual empty manifest");

	hasLoadedActions = true;

	// We have to set this after calling BindInputsForSession, as otherwise BaseCompositor::Submit might
	// be called concurrently, and use the inputs we haven't set properly.
	// See issue #408.
	usingLegacyInput = false;

	// TODO deduplicate with the regular manifest loader
	CreateLegacyActions();

	// Load in the suggested bindings for the legacy input actions
	for (const std::unique_ptr<InteractionProfile>& profile : InteractionProfile::GetProfileList()) {
		if (!profile->CanHaveBindings())
			continue;

		std::vector<XrActionSuggestedBinding> bindings;
		for (const auto& legacyController : legacyControllers) {
			profile->AddLegacyBindings(legacyController, bindings);
		}

		// Load the bindings into the runtime
		XrPath interactionProfilePath = XR_NULL_PATH;
		OC_XR_LOG(xrStringToPath(xr_instance, profile->GetPath().c_str(), &interactionProfilePath));
		XrInteractionProfileSuggestedBinding suggestedBindings{ XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };

		suggestedBindings.interactionProfile = interactionProfilePath;
		suggestedBindings.suggestedBindings = bindings.data();
		suggestedBindings.countSuggestedBindings = bindings.size();
		OC_XR_LOG(xrSuggestInteractionProfileBindings(xr_instance, &suggestedBindings));
	}

	// Attach everything to the current session
	BindInputsForSession();

	// Finally mark the inputs as being set up
	usingLegacyInput = true;
}

void BaseInput::BindInputsForSession()
{
	// This is called from DrvOpenXR::SetupSession. If we requested a session restart ourselves, we're also
	// going to bind the inputs ourselves anyway, so we don't want to do that twice.
	// If we didn't request the restart but we also haven't even loaded actions yet, there's nothing to bind yet.
	if (restartingSession || !hasLoadedActions)
		return;

	// Since the session has changed, any actionspaces we previously created are now invalid
	for (const std::unique_ptr<Action>& action : actions.GetItems()) {
		action->actionSpaces.clear();
	}

	// Same goes for the actionspaces of the legacy controller pose actions, this time create
	// new ones for this session.
	for (LegacyControllerActions& lca : legacyControllers) {
		// No need to destroy it, the session it was attached to was destroyed
		lca.gripPoseSpace = XR_NULL_HANDLE;
		lca.aimPoseSpace = XR_NULL_HANDLE;

		// Create the new spaces
		XrActionSpaceCreateInfo info = { XR_TYPE_ACTION_SPACE_CREATE_INFO };
		info.poseInActionSpace = S2O_om34_pose(G2S_m34(glm::identity<glm::mat4>()));
		info.action = lca.gripPoseAction;
		OC_XR_LOG(xrCreateActionSpace(xr_session.get(), &info, &lca.gripPoseSpace));
		info.action = lca.aimPoseAction;
		OC_XR_LOG(xrCreateActionSpace(xr_session.get(), &info, &lca.aimPoseSpace));
	}

	// Note: even if actionSets is empty, we always still want to load the legacy set.

	// Now attach the action sets to the OpenXR session, making them immutable (including attaching suggested bindings)
	std::vector<XrActionSet> sets;
	for (const std::unique_ptr<ActionSet>& as : actionSets.GetItems()) {
		sets.push_back(as->xr);
	}

	sets.push_back(legacyInputsSet);

	// A set OpenXR refused to create is left out, or attaching would fail for all of them
	sets.erase(std::remove(sets.begin(), sets.end(), (XrActionSet)XR_NULL_HANDLE), sets.end());
	XrSessionActionSetsAttachInfo attachInfo = { XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
	attachInfo.actionSets = sets.data();
	attachInfo.countActionSets = sets.size();
	OC_XR_LOG(xrAttachSessionActionSets(xr_session.get(), &attachInfo));

	// Setup hand tracking if supported
	if (xr_gbl->handTrackingProperties.supportsHandTracking) {
		XrHandTrackerCreateInfoEXT createInfo = { XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT };
		for (int i = 0; i < 2; i++) {
			createInfo.hand = i == 0 ? XR_HAND_LEFT_EXT : XR_HAND_RIGHT_EXT;
			createInfo.handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT;
			OC_XR_LOG(xr_ext->xrCreateHandTrackerEXT(xr_session.get(), &createInfo, &handTrackers[i]));
		}
	}
}

void BaseInput::LoadBindingsSet(const InteractionProfile& profile, const std::string& bindingsPath)
{
	OOVR_LOGF("Loading bindings for %s", profile.GetPath().c_str());

	if (!profile.CanHaveBindings())
		return;

	// A binding file that can't be used leaves this controller type without bindings; the others still load
	Json::Value bindingsRoot;
	std::string bindingsError;
	if (!ReadJson(bindingsPath, bindingsRoot, bindingsError) || !bindingsRoot.isObject()) {
		OOVR_LOGF("Warning: can't read the binding file, no bindings for %s: %s", profile.GetPath().c_str(),
		    bindingsError.empty() ? "not a JSON object" : bindingsError.c_str());
		return;
	}

	// TODO aliases, if anyone uses them
	if (!bindingsRoot["alias_info"].empty())
		OOVR_LOGF("WARNING: Ignoring alias_info from binding descriptor %s", bindingsPath.c_str());

	const Json::Value& bindingsJson = bindingsRoot["bindings"];
	if (!bindingsJson.isObject()) {
		OOVR_LOGF("Warning: bindings file %s has no bindings object, no bindings for %s", bindingsPath.c_str(), profile.GetPath().c_str());
		return;
	}

	// Index controllers require special handling of grip and touchpads
	auto isKnuckles = bindingsRoot["controller_type"] == "knuckles";
	if (isKnuckles)
		OOVR_LOGF("Bindings detected as knuckles, fixups enabled (%s)", bindingsPath.c_str());

	std::vector<XrActionSuggestedBinding> bindings;

	for (std::string setFullName : bindingsJson.getMemberNames()) try {
		const Json::Value setJson = bindingsJson[setFullName];

		setFullName = lowerStr(setFullName);

		std::string prefix = "/actions/";
		if (strncmp(prefix.c_str(), setFullName.c_str(), prefix.size()) != 0) {
			OOVR_LOGF("Warning: skipping action set '%s' in bindings file '%s' - missing or bad prefix", setFullName.c_str(), bindingsPath.c_str());
			continue;
		}
		std::string setName = setFullName.substr(prefix.size());

		ActionSet* set = actionSets.LookupItem("/actions/" + setName);
		if (set == nullptr) {
			// It seems ActionSets specified in the bindings but not in the action manifest are just ignored.
			// Jet Island has those as of 29/05/2022.
			OOVR_LOGF("WARNING: Missing action set '%s' in bindings file '%s', ignoring", setName.c_str(), bindingsPath.c_str());
			continue;
		}

		// TODO combine these loops for sources, poses and haptics

		for (const auto& srcJson : setJson["sources"]) try {
			if (!srcJson.isObject()) {
				OOVR_LOGF("Warning: skipping a source that isn't a JSON object in bindings file '%s'", bindingsPath.c_str());
				continue;
			}
			std::string importBasePath = lowerStr(srcJson["path"].asString());

			const Json::Value& inputsJson = srcJson["inputs"];
			for (const std::string& inputName : inputsJson.getMemberNames()) try {
				const Json::Value item = inputsJson[inputName];

				std::string actionName = actions.ShortenOrLookupName(item["output"].asString());
				Action* action = actions.LookupItem(actionName);

				// For some reason, No Man's Sky has actions that don't exist in the manifest in its binding files
				if (action == nullptr) {
					OOVR_LOGF("WARNING: Action '%s' (from binding file %s) does not exist in manifest, skipping", actionName.c_str(), bindingsPath.c_str());
					continue;
				}

				if (srcJson["mode"].asString() == "dpad") {
					LoadDpadAction(profile, importBasePath, inputName, srcJson["parameters"]["sub_mode"].asString(), action, bindings, isKnuckles);
					continue;
				}

				if (srcJson["mode"].asString() == "button") {
					if (inputName == "double") {
						LoadDClickAction(profile, importBasePath, action, bindings);
						continue;
					}

					// special case: in the default bindings for HL:A, the "walk" action is bound with the "click" input and has an extra
					// "force_input" parameter, here we just treat this binding as always returning true to allow thumbsticks to work (not sure if this is correct)
					if (srcJson["parameters"]["force_input"] == "position") {
						for (uint32_t i = 0; i < allSubactionPathNames.size(); i++) {
							if (importBasePath.starts_with(allSubactionPathNames[i]))
								action->perProfileData[&profile].forcedSubactionPaths.emplace_back(allSubactionPaths[i]);
						}

						continue;
					}
				}

				bool knucklesGripMainInput = false;
				// Don't activate split grip handling anywhere but knuckles
				if (isKnuckles && srcJson["mode"].asString() == "grab" && importBasePath.ends_with("grip")) {
					float activateValue = 0.7f;
					float releaseValue = 0.65f; // These are SteamVR defaults

					if (!srcJson["parameters"]["value_hold_threshold"].empty())
						activateValue = std::stof(srcJson["parameters"]["value_hold_threshold"].asString().c_str());

					if (!srcJson["parameters"]["value_release_threshold"].empty())
						releaseValue = std::stof(srcJson["parameters"]["value_release_threshold"].asString().c_str());

					action->perProfileData[&profile].click_activate_threshold = activateValue;
					action->perProfileData[&profile].click_deactivate_threshold = releaseValue;

					const auto forceActionKey = action->setName + "-" + action->shortName;
					auto existingAction = indexGripExtensionActions.find(forceActionKey);
					if (existingAction == indexGripExtensionActions.end()) {
						XrAction forceAction = XR_NULL_HANDLE;

						XrActionCreateInfo info{ XR_TYPE_ACTION_CREATE_INFO };
						info.subactionPaths = allSubactionPaths.data();
						info.countSubactionPaths = allSubactionPaths.size();
						std::string extends_name = forceActionKey + "-grip-force-extra";
						strcpy_arr(info.actionName, extends_name.c_str());
						info.actionType = XR_ACTION_TYPE_FLOAT_INPUT;
						strcpy_arr(info.localizedActionName, extends_name.c_str());
						OC_XR_LOG(xrCreateAction(action->set->xr, &info, &forceAction));

						indexGripExtensionActions[forceActionKey] = forceAction;
						existingAction = indexGripExtensionActions.find(forceActionKey);
					}

					auto forceAction = existingAction->second;
					auto pathStr = profile.TranslateAction(importBasePath + "/force");
					if (!profile.IsInputPathValid(pathStr)) {
						OOVR_LOGF("Trying to bind index extendo action to non-existent path %s? Ignoring...", pathStr.c_str());
					} else {
						OOVR_LOGF("Bound index extendo action to %s for original %s (%s)", pathStr.c_str(), importBasePath.c_str(), action->fullName.c_str());
						XrPath path = XR_NULL_PATH;
						OC_XR_LOG(xrStringToPath(xr_instance, pathStr.c_str(), &path));
						AddSuggestedBinding(bindings, forceAction, path);
					}

					action->stackedValueExtension = forceAction;

					knucklesGripMainInput = true;
				}

				for (auto& parameter_name : srcJson["parameters"].getMemberNames()) {
					OOVR_LOGF("Parameter: %s=%s", parameter_name.c_str(), srcJson["parameters"][parameter_name].asString().c_str());
					if (parameter_name == "click_activate_threshold") {
						action->perProfileData[&profile].click_activate_threshold = std::stof(srcJson["parameters"][parameter_name].asString().c_str());
					}
					if (parameter_name == "click_deactivate_threshold") {
						action->perProfileData[&profile].click_deactivate_threshold = std::stof(srcJson["parameters"][parameter_name].asString().c_str());
					}
					if (parameter_name == "deadzone_pct") {
						action->perProfileData[&profile].deadzone = std::stof(srcJson["parameters"][parameter_name].asString().c_str()) / 100.f;
					}
					if (parameter_name == "maxzone_pct") {
						action->perProfileData[&profile].maxzone = std::stof(srcJson["parameters"][parameter_name].asString().c_str()) / 100.f;
					}
				}

				// Translate path string to an appropriate path supported by this interaction profile, if necessary
				auto buildPath = [&](const std::string& basePath) {
					// special case: "position" in OpenVR is a 2D vector, in OpenXR this can be represented by the parent of the input value
					// See the OpenXR spec section 11.4 ("Suggested Bindings")
					if (inputName == "position")
						return profile.TranslateAction(basePath);
					if (inputName == "click" && basePath.find("trigger") != std::string::npos) {
						// vive wands only need this for the trigger to do thr thresholds
						OOVR_LOGF("Using input value instead of click");
						return profile.TranslateAction(basePath + "/" + "value");
					}
					if (knucklesGripMainInput)
						return profile.TranslateAction(basePath + "/value");
					return profile.TranslateAction(basePath + "/" + inputName);
				};
				std::string pathStr = buildPath(importBasePath);

				// A source this controller has no input of that name for: its counterpart here, if it has one.
				// This only adds bindings the translation above dropped; every binding it made stays as it was.
				// OC_NO_BINDING_CONVERSION turns it off, for the check that it only adds (check_bindings_superset.py)
				static const bool convert = !getenv("OC_NO_BINDING_CONVERSION");
				if (convert && !profile.IsInputPathValid(pathStr) && profile.GetOpenVRName()) {
					std::string fileType = lowerStr(bindingsRoot["controller_type"].asString());
					if (std::optional<std::string> counterpart = ConvertSourcePath(importBasePath, fileType, profile.GetOpenVRName().value())) {
						std::string converted = buildPath(*counterpart);
						if (profile.IsInputPathValid(converted)) {
							OOVR_LOGF("Bound %s (a %s input) to its counterpart %s on %s", importBasePath.c_str(), fileType.c_str(),
							    converted.c_str(), profile.GetPath().c_str());
							pathStr = converted;
						}
					}
				}

				if (!profile.IsInputPathValid(pathStr)) {
					OOVR_LOGF("WARNING: Built invalid input path %s for profile %s from action %s, skipping",
					    pathStr.c_str(), profile.GetPath().c_str(), actionName.c_str());
					continue;
				}

				XrPath path = XR_NULL_PATH;
				OC_XR_LOG(xrStringToPath(xr_instance, pathStr.c_str(), &path));
				AddSuggestedBinding(bindings, action->xr, path);
			} catch (const std::exception& e) {
				// A wrong-typed value in this entry (Json::LogicError) or a bad number: only the entry is left out
				OOVR_SOFT_ABORTF("Skipping an input of bindings file '%s': %s", bindingsPath.c_str(), e.what());
			}
		} catch (const std::exception& e) {
			OOVR_SOFT_ABORTF("Skipping a source of bindings file '%s': %s", bindingsPath.c_str(), e.what());
		}

		for (const auto& item : setJson["poses"]) try {
			if (!item.isObject()) {
				OOVR_LOGF("Warning: skipping a pose binding that isn't a JSON object in bindings file '%s'", bindingsPath.c_str());
				continue;
			}
			std::string specPath = lowerStr(item["path"].asString());

			std::string actionName = lowerStr(item["output"].asString());
			Action* action = actions.LookupItem(actionName);
			if (action == nullptr) {
				OOVR_LOGF("Missing action '%s' in bindings file '%s'", actionName.c_str(), bindingsPath.c_str());
				continue;
			}

			PoseBindingInfo info;

			// Find which hand this is from - on a path like /user/hand/left/pose/raw, after this
			// we're left with pose/raw.
			std::string withoutPrefix = specPath;
			info.hand = ParseAndRemoveHandPrefix(withoutPrefix);
			if (info.hand == ITrackedDevice::HAND_NONE) {
				OOVR_LOGF("WARNING: Ignoring invalid pose binding '%s' for %s, bad hand prefix", specPath.c_str(), action->fullName.c_str());
				continue;
			}

			// Parse the paths, matching them up with the components they correspond to
			if (withoutPrefix == "pose/raw" || withoutPrefix == "pose/aim") {
				info.point = PoseBindingPoint::RAW;
			} else if (withoutPrefix == "pose/grip") {
				info.point = PoseBindingPoint::GRIP;
			} else if (withoutPrefix == "pose/handgrip") {
				info.point = PoseBindingPoint::HANDGRIP;
			} else if (withoutPrefix == "pose/base") {
				info.point = PoseBindingPoint::BASE;
			} else if (withoutPrefix == "pose/tip") {
				info.point = PoseBindingPoint::TIP;
			} else if (withoutPrefix == "pose/body") {
				info.point = PoseBindingPoint::BODY;
			} else if (withoutPrefix == "pose/gdc2015") {
				info.point = PoseBindingPoint::GDC2015;
			} else {
				OOVR_LOGF("WARNING: Ignoring unknown pose path '%s' (%s) for action %s", specPath.c_str(), withoutPrefix.c_str(), action->fullName.c_str());
				continue;
			}

			// We only accommodate one binding per hand, since having multiple doesn't make sense - either
			// a hand is tracked or not, since we derive everything from the grip pose.
			if (info.hand == ITrackedDevice::HAND_LEFT) {
				action->poseBindingsLeft[&profile] = info;
			} else {
				action->poseBindingsRight[&profile] = info;
			}
		} catch (const std::exception& e) {
			OOVR_SOFT_ABORTF("Skipping a pose binding of bindings file '%s': %s", bindingsPath.c_str(), e.what());
		}

		for (const auto& item : setJson["haptics"]) try {
			if (!item.isObject()) {
				OOVR_LOGF("Warning: skipping a haptic binding that isn't a JSON object in bindings file '%s'", bindingsPath.c_str());
				continue;
			}
			std::string pathStr = lowerStr(item["path"].asString());

			std::string actionName = lowerStr(item["output"].asString());
			Action* action = actions.LookupItem(actionName);
			// A haptic binding for an action the manifest doesn't have, or to a path the controller doesn't have, is skipped
			if (action == nullptr) {
				OOVR_LOGF("Warning: skipping haptic binding for missing action '%s' in bindings file '%s'", actionName.c_str(), bindingsPath.c_str());
				continue;
			}

			if (!profile.IsInputPathValid(pathStr)) {
				OOVR_LOGF("Warning: skipping haptic binding to invalid path %s for action %s", pathStr.c_str(), actionName.c_str());
				continue;
			}

			XrPath path = XR_NULL_PATH;
			OC_XR_LOG(xrStringToPath(xr_instance, pathStr.c_str(), &path));
			AddSuggestedBinding(bindings, action->xr, path);
		} catch (const std::exception& e) {
			OOVR_SOFT_ABORTF("Skipping a haptic binding of bindings file '%s': %s", bindingsPath.c_str(), e.what());
		}
	} catch (const std::exception& e) {
		OOVR_SOFT_ABORTF("Skipping an action set of bindings file '%s': %s", bindingsPath.c_str(), e.what());
	}

	// If there aren't any bindings, that makes it simple
	// If we didn't return then xrSuggestInteractionProfileBindings would fail
	if (bindings.empty()) {
		return;
	}

	// Only register legacy bindings if this profile is supported by the application. This is to avoid the runtime picking
	// an input profile we know about but the game doesn't (eg khr/simple_controller) over one that the game has actually
	// defined bindings for (eg htc/vive_controller).
	for (const auto& legacyController : legacyControllers) {
		profile.AddLegacyBindings(legacyController, bindings);
	}

	// Load the bindings we just built into the runtime
	XrPath interactionProfilePath = XR_NULL_PATH;
	OC_XR_LOG(xrStringToPath(xr_instance, profile.GetPath().c_str(), &interactionProfilePath));

	XrInteractionProfileSuggestedBinding suggestedBindings{ XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
	suggestedBindings.interactionProfile = interactionProfilePath;
	suggestedBindings.suggestedBindings = bindings.data();
	suggestedBindings.countSuggestedBindings = bindings.size();
	OC_XR_LOG(xrSuggestInteractionProfileBindings(xr_instance, &suggestedBindings));
}

void BaseInput::LoadDpadAction(const InteractionProfile& profile, const std::string& importBasePath, const std::string& inputName, const std::string& subMode, Action* action, std::vector<XrActionSuggestedBinding>& bindings, bool isKnuckles)
{
	// special case for dpad: we need to create additional inputs and read them ourselves
	// verify that we actually have an input that can be used as an dpad for this profile
	std::string parentPath = profile.TranslateAction(importBasePath);
	if (!profile.IsInputPathValid(parentPath)) {
		OOVR_LOGF("WARNING: No such input path %s for profile %s, cannot bind dpad inputs, skipping", parentPath.c_str(), profile.GetPath().c_str());
		return;
	}

	// check direction
	auto dir_iter = DpadBindingInfo::directionMap.find(inputName);
	if (dir_iter == DpadBindingInfo::directionMap.end()) {
		OOVR_LOGF("WARNING: Unknown dpad direction %s given, skipping binding", inputName.c_str());
		return;
	}
	DpadBindingInfo dpad_info;
	dpad_info.direction = dir_iter->second;

	if (subMode != "touch" && subMode != "click") {
		OOVR_LOGF("WARNING: Unknown dpad sub mode %s given, skipping binding", subMode.c_str());
		return;
	}

	// check if parent is in dpadBindingParents
	// get parent name: remove /user/hand and /input/ parts, add end of openxr path (so we don't i.e. confuse dpad bindings from the knuckles joystick with dpad bindings from the oculus joystick)
	std::string to_delete[] = { "/user/hand/", "/input/" };
	auto& path = profile.GetPath();
	std::string end = path.substr(path.rfind("/") + 1);
	std::string parentName = importBasePath + "-" + end + "-dpad-parent";
	for (const auto& str : to_delete) {
		parentName.replace(parentName.find(str), str.size(), "");
	}

	XrActionCreateInfo info{ XR_TYPE_ACTION_CREATE_INFO };
	// dpads can be on either hand: need to set subaction paths for GetAnalogActionData
	info.subactionPaths = allSubactionPaths.data();
	info.countSubactionPaths = allSubactionPaths.size();

	// The parent actions belong to this action's set, and an action set's actions are only active
	// while the set is: so each set gets its own. Shared between sets, they lived in whichever set
	// bound the input first, and a dpad in any other set never fired (No Man's Sky: its menu set
	// came first, so snap turning and teleporting on foot did nothing).
	std::string parentKey = action->setName + ":" + parentName;
	auto parent_iter = DpadBindingInfo::parents.find(parentKey);
	if (parent_iter == DpadBindingInfo::parents.end()) {
		// create mapping
		DpadBindingInfo::parents.insert({ parentKey, DpadBindingInfo::ParentActions{} });
		parent_iter = DpadBindingInfo::parents.find(parentKey);

		// create action for getting parent data (i.e. trackpad location)
		strcpy_arr(info.actionName, parentName.c_str());
		info.actionType = XR_ACTION_TYPE_VECTOR2F_INPUT;
		strcpy_arr(info.localizedActionName, parentName.c_str()); // TODO localization
		OC_XR_LOG(xrCreateAction(action->set->xr, &info, &parent_iter->second.vectorAction));

		// add parent to bindings
		XrPath suggested_path = XR_NULL_PATH;
		OC_XR_LOG(xrStringToPath(xr_instance, profile.TranslateAction(importBasePath).c_str(), &suggested_path));
		AddSuggestedBinding(bindings, parent_iter->second.vectorAction, suggested_path);
	}

	if (subMode == "click") {
		dpad_info.click = true;

		// Every binding on this input needs these, not only the one that creates the shared click action below:
		// on the Knuckles trackpad that action is a float, and reading it as a boolean fails.
		// Knuckles bindings are also loaded for other controllers, which have no trackpad force
		auto manualThreshold = isKnuckles && importBasePath.ends_with("trackpad")
		    && profile.IsInputPathValid(profile.TranslateAction(importBasePath + "/force"));
		dpad_info.customClickThresholds = manualThreshold;
		dpad_info.triggerHapticOnClick = manualThreshold;

		if (parent_iter->second.clickAction == XR_NULL_HANDLE) {
			std::string click_name = parentName + "-click";
			strcpy_arr(info.actionName, click_name.c_str());
			info.actionType = manualThreshold ? XR_ACTION_TYPE_FLOAT_INPUT : XR_ACTION_TYPE_BOOLEAN_INPUT;
			strcpy_arr(info.localizedActionName, click_name.c_str());
			OC_XR_LOG(xrCreateAction(action->set->xr, &info, &parent_iter->second.clickAction));

			XrPath suggested_path = XR_NULL_PATH;
			OC_XR_LOG(xrStringToPath(xr_instance, profile.TranslateAction(importBasePath + (manualThreshold ? "/force" : "/click")).c_str(), &suggested_path));
			AddSuggestedBinding(bindings, parent_iter->second.clickAction, suggested_path);
		}
	} else {
		// touch dpad
		dpad_info.click = false;
		std::string touchPath = profile.TranslateAction(importBasePath + "/touch");
		if (!profile.IsInputPathValid(touchPath)) {
			OOVR_LOGF("WARNING: Path %s does not exist for the touch dpad.", touchPath.c_str());
		} else if (parent_iter->second.touchAction == XR_NULL_HANDLE) {
			std::string touch_name = parentName + "-touch";
			strcpy_arr(info.actionName, touch_name.c_str());
			info.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
			strcpy_arr(info.localizedActionName, touch_name.c_str());
			OC_XR_LOG(xrCreateAction(action->set->xr, &info, &parent_iter->second.touchAction));

			XrPath suggested_path = XR_NULL_PATH;
			OC_XR_LOG(xrStringToPath(xr_instance, touchPath.c_str(), &suggested_path));
			AddSuggestedBinding(bindings, parent_iter->second.touchAction, suggested_path);
		}
	}

	// add dpad parent to action
	action->perProfileData[&profile].dpadBindings.push_back({ parentKey, dpad_info });
}

void BaseInput::LoadDClickAction(const InteractionProfile& profile, const std::string& importBasePath, Action* action, std::vector<XrActionSuggestedBinding>& bindings)
{
	std::string clickPath = profile.TranslateAction(importBasePath + "/click");

	if (!profile.IsInputPathValid(clickPath)) {
		OOVR_LOGF("WARNING: No such input path %s for profile %s, cannot bind double input, skipping", clickPath.c_str(), profile.GetPath().c_str());
		return;
	}

	DClickBindingInfo dclickInfo;
	dclickInfo.first_click_time = 0;
	dclickInfo.last_state = false;

	// create the source click action
	XrActionCreateInfo info{ XR_TYPE_ACTION_CREATE_INFO };
	info.subactionPaths = allSubactionPaths.data();
	info.countSubactionPaths = allSubactionPaths.size();

	// same as dpad action names
	std::string to_delete[] = { "/user/hand/", "/input/" };
	auto& path = profile.GetPath();
	std::string end = path.substr(path.rfind("/") + 1);
	std::string clickName = importBasePath + "-" + end + "-double-click";
	for (const auto& str : to_delete) {
		clickName.replace(clickName.find(str), str.size(), "");
	}

	info.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
	strcpy_arr(info.actionName, clickName.c_str());
	strcpy_arr(info.localizedActionName, clickName.c_str()); // TODO: localization
	OC_XR_LOG(xrCreateAction(action->set->xr, &info, &dclickInfo.click_action)); // FIXME: share click actions

	XrPath suggested_path = XR_NULL_PATH;
	OC_XR_LOG(xrStringToPath(xr_instance, clickPath.c_str(), &suggested_path));
	AddSuggestedBinding(bindings, dclickInfo.click_action, suggested_path);

	action->perProfileData[&profile].dclickBindings.push_back(dclickInfo);
}

void BaseInput::CreateLegacyActions()
{
	// Add the stuff required to make the backend work
	// This means the pose and haptic inputs for each hand, and actions for all the legacy inputs, into a new ActionSet that
	// is always active with a lower priority than the game's one.

	XrActionSetCreateInfo setInfo = { XR_TYPE_ACTION_SET_CREATE_INFO };
	strcpy_arr(setInfo.actionSetName, "opencomposite-actions");
	strcpy_arr(setInfo.localizedActionSetName, "OpenComposite Actions");
	setInfo.priority = 0;

	OC_XR_LOG(xrCreateActionSet(xr_instance, &setInfo, &legacyInputsSet));

	for (int i = 0; i < 2; i++) {
		LegacyControllerActions& ctrl = legacyControllers[i];
		ctrl = {};

		std::string side = i == 0 ? "left" : "right";
		ctrl.handPath = "/user/hand/" + side;
		ctrl.handType = i == 0 ? ITrackedDevice::HAND_LEFT : ITrackedDevice::HAND_RIGHT;
		OC_XR_LOG(xrStringToPath(xr_instance, ctrl.handPath.c_str(), &ctrl.handPathXr));

		auto create = [&](XrAction* out, const std::string& name, const std::string& englishName, XrActionType type) {
			XrActionCreateInfo info = { XR_TYPE_ACTION_CREATE_INFO };
			info.actionType = type;

			std::string codeName = "legacy-" + side + "-" + name;
			strcpy_arr(info.actionName, codeName.c_str());

			std::string fullHumanName = "Legacy input: " + englishName + " - " + side; // TODO localisation
			strcpy_arr(info.localizedActionName, fullHumanName.c_str());

			OC_XR_LOG(xrCreateAction(legacyInputsSet, &info, out));
		};

		create(&ctrl.system, "system", "System Button", XR_ACTION_TYPE_BOOLEAN_INPUT);
		create(&ctrl.menu, "menu", "Menu Button", XR_ACTION_TYPE_BOOLEAN_INPUT);
		create(&ctrl.menuTouch, "menu-touch", "Menu Button (Touch)", XR_ACTION_TYPE_BOOLEAN_INPUT);
		create(&ctrl.btnA, "btn-a", "'A' Button", XR_ACTION_TYPE_BOOLEAN_INPUT);
		create(&ctrl.btnATouch, "btn-a-touch", "'A' Button (Touch)", XR_ACTION_TYPE_BOOLEAN_INPUT);

		create(&ctrl.stickBtn, "thumbstick-btn", "Thumbstick Button", XR_ACTION_TYPE_BOOLEAN_INPUT);
		create(&ctrl.stickBtnTouch, "thumbstick-btn-touch", "Thumbstick Button (Touch)", XR_ACTION_TYPE_BOOLEAN_INPUT);
		create(&ctrl.stickX, "thumbstick-x", "Thumbstick X axis", XR_ACTION_TYPE_FLOAT_INPUT);
		create(&ctrl.stickY, "thumbstick-y", "Thumbstick Y axis", XR_ACTION_TYPE_FLOAT_INPUT);
		create(&ctrl.trackpadX, "trackpad-x", "Trackpad X axis", XR_ACTION_TYPE_FLOAT_INPUT);
		create(&ctrl.trackpadY, "trackpad-y", "Trackpad Y axis", XR_ACTION_TYPE_FLOAT_INPUT);
		create(&ctrl.trackpadTouch, "trackpad-touch", "Trackpad Touch", XR_ACTION_TYPE_BOOLEAN_INPUT);
		create(&ctrl.trackpadForce, "trackpad-force", "Trackpad Force", XR_ACTION_TYPE_FLOAT_INPUT);

		// Note that while we define the grip as a float, we can still bind it to boolean actions and the OpenXR runtime will
		// return 0.0 or 1.0 depending on the button status. OpenXR 1.0 § 11.4.
		create(&ctrl.grip, "grip", "Grip", XR_ACTION_TYPE_FLOAT_INPUT);
		create(&ctrl.gripClick, "grip-click", "Grip (Digital)", XR_ACTION_TYPE_BOOLEAN_INPUT);
		create(&ctrl.trigger, "trigger", "Trigger", XR_ACTION_TYPE_FLOAT_INPUT);
		create(&ctrl.triggerTouch, "trigger-touch", "Trigger (Touch)", XR_ACTION_TYPE_BOOLEAN_INPUT);
		create(&ctrl.triggerClick, "trigger-click", "Trigger (Digital)", XR_ACTION_TYPE_BOOLEAN_INPUT);

		create(&ctrl.haptic, "haptic", "Vibration Haptics", XR_ACTION_TYPE_VIBRATION_OUTPUT);

		create(&ctrl.gripPoseAction, "grip-pose", "Grip Pose", XR_ACTION_TYPE_POSE_INPUT);
		create(&ctrl.aimPoseAction, "aim-pose", "Aim Pose", XR_ACTION_TYPE_POSE_INPUT);
	}
}

EVRInputError BaseInput::GetActionSetHandle(const char* pchActionSetName, VRActionSetHandle_t* pHandle)
{
	if (!pchActionSetName || !pHandle)
		return VRInputError_InvalidParam;
	*pHandle = actionSets.LookupHandle(pchActionSetName);
	return vr::VRInputError_None;
}

EVRInputError BaseInput::GetActionHandle(const char* pchActionName, VRActionHandle_t* pHandle)
{
	if (!pchActionName || !pHandle)
		return VRInputError_InvalidParam;
	*pHandle = actions.LookupHandle(pchActionName);
	return vr::VRInputError_None;
}

EVRInputError BaseInput::GetInputSourceHandle(const char* pchInputSourcePath, VRInputValueHandle_t* pHandle)
{
	// Get the existing InputValueHandle if it already exists, or make a new one otherwise. Applications can
	// get whatever handles they want, regardless of whether the runtime associates any special meaning with it.

	if (!pchInputSourcePath || !pHandle)
		return VRInputError_InvalidParam;

	const auto iter = inputHandleRegistry.find(pchInputSourcePath);
	if (iter != inputHandleRegistry.end()) {
		InputValueHandle* handle = iter->second.get();
		*pHandle = (uint64_t)handle;
		return VRInputError_None;
	}

	std::unique_ptr<InputValueHandle> handle = std::make_unique<InputValueHandle>();
	InputValueHandle* ptr = handle.get();
	handle->path = pchInputSourcePath;

	// Yes, this will let through something like/user/hand/leftblah but it's probably not an issue
	static std::string leftHand = "/user/hand/left";
	static std::string rightHand = "/user/hand/right";
	if (strncmp(pchInputSourcePath, leftHand.c_str(), leftHand.size()) == 0) {
		handle->type = InputSource::HAND_LEFT;
		handle->devicePathString = leftHand;
	}
	if (strncmp(pchInputSourcePath, rightHand.c_str(), rightHand.size()) == 0) {
		handle->type = InputSource::HAND_RIGHT;
		handle->devicePathString = rightHand;
	}

	if (!handle->devicePathString.empty()) {
		// Without an OpenXR instance (a Utility app) the handle still works; it just matches no subaction path
		XrResult res = xr_instance ? xrStringToPath(xr_instance, handle->devicePathString.c_str(), &handle->devicePath) : XR_ERROR_HANDLE_INVALID;
		if (XR_FAILED(res)) {
			OOVR_LOGF("GetInputSourceHandle: no OpenXR path for %s (%d)", handle->devicePathString.c_str(), (int)res);
			handle->devicePath = XR_NULL_PATH;
		} else if (!std::count(allSubactionPaths.begin(), allSubactionPaths.end(), handle->devicePath)) {
			// Very much should always be true
			OOVR_LOGF("GetInputSourceHandle: %s isn't a hand's path", handle->devicePathString.c_str());
		}
	}

	inputHandleRegistry[pchInputSourcePath] = std::move(handle);
	*(uint64_t*)pHandle = (uint64_t)ptr;
	return VRInputError_None;
}

EVRInputError BaseInput::UpdateActionState(VR_ARRAY_COUNT(unSetCount) VRActiveActionSet_t* pSets,
    uint32_t unSizeOfVRSelectedActionSet_t, uint32_t unSetCount)
{
	// TODO if the game is using legacy input, call this every frame

	// A null or wrong-sized struct is an argument error, as SteamVR answers (VRInputError_InvalidParam)
	if (sizeof(*pSets) != unSizeOfVRSelectedActionSet_t || (unSetCount && !pSets))
		return VRInputError_InvalidParam;

	// Make sure all the ActionSets have the same priority, since we don't have any way around that right now
	if (unSetCount > 1) {
		int priority = pSets[0].nPriority;
		for (uint32_t i = 1; i < unSetCount; i++) {
			if (pSets[i].nPriority != priority) {
				ActionSet* as1 = cast_ASH(pSets[0].ulActionSet);
				ActionSet* curAs = cast_ASH(pSets[i].ulActionSet);
				if (as1 && curAs)
					OOVR_SOFT_ABORTF("Active action set %s (%d) and %s (%d) have different priorities, this is not yet supported",
					    as1->fullName.c_str(), pSets[0].nPriority, curAs->fullName.c_str(), pSets[i].nPriority);
			}
		}
	}

	std::vector<XrActiveActionSet> aas(unSetCount + 1);

	for (uint32_t i = 0; i < unSetCount; i++) {
		VRActiveActionSet_t& set = pSets[i];

		GET_ACTION_SET_FROM_HANDLE(as, set.ulActionSet);
		aas[i].actionSet = as->xr;

		if (set.ulRestrictedToDevice != vr::k_ulInvalidInputValueHandle) {
			std::shared_ptr<ITrackedDevice> dev = ivhToDev(set.ulRestrictedToDevice);
			if (dev && dev->GetHand() != ITrackedDevice::HAND_NONE) {
				LegacyControllerActions& ctrl = legacyControllers[dev->GetHand()];
				aas[i].subactionPath = ctrl.handPathXr;
			}
		}
	}

	// Ad the last set, the legacy input set
	aas.at(unSetCount).actionSet = legacyInputsSet;

	// A set OpenXR refused to create is left out, or the whole sync would fail
	aas.erase(std::remove_if(aas.begin(), aas.end(), [](const XrActiveActionSet& a) { return a.actionSet == XR_NULL_HANDLE; }), aas.end());

	XrActionsSyncInfo syncInfo = { XR_TYPE_ACTIONS_SYNC_INFO };
	syncInfo.activeActionSets = aas.data();
	syncInfo.countActiveActionSets = aas.size();
	OC_XR_LOG(xrSyncActions(xr_session.get(), &syncInfo));
	syncSerial++;

	for (size_t i = 0; i < allSubactionPaths.size(); i++) {
		auto handDevice = BackendManager::Instance().GetBackendInstance()->GetDeviceByHand(static_cast<ITrackedDevice::TrackedDeviceType>(i));
		auto newProfile = handDevice == nullptr ? nullptr : handDevice->GetInteractionProfile();
		if (handInteractionProfiles[i] != newProfile) {
			OOVR_LOGF("Hand %s switching to profile %s", allSubactionPathNames[i].c_str(), newProfile == nullptr ? "<null>" : newProfile->GetPath().c_str());
			handInteractionProfiles[i] = newProfile;
		}
	}

	return VRInputError_None;
}

void BaseInput::InternalUpdate()
{
	// Always increment this once per frame, and only once per frame
	syncSerialDigital++;

	if (!usingLegacyInput)
		return;

	XrActiveActionSet aas = { legacyInputsSet };

	XrActionsSyncInfo syncInfo = { XR_TYPE_ACTIONS_SYNC_INFO };
	syncInfo.activeActionSets = &aas;
	syncInfo.countActiveActionSets = 1;
	OC_XR_LOG(xrSyncActions(xr_session.get(), &syncInfo));
	syncSerial++;
}

XrResult BaseInput::getBooleanOrDpadData(const InteractionProfile* profile, Action& action, const XrActionStateGetInfo* getInfo, XrActionStateBoolean* state)
{
	const auto& perProfileData = action.perProfileData.contains(profile) ? action.perProfileData[profile] : Action::defaultActionData;
	for (auto forced_path : perProfileData.forcedSubactionPaths) {
		if (forced_path != getInfo->subactionPath)
			continue;

		state->currentState = XR_TRUE;
		state->isActive = XR_TRUE;

		state->lastChangeTime = 0;
		state->changedSinceLastSync = XR_FALSE; // TODO: is initial rising edge needed for first query?

		return XR_SUCCESS;
	}

	// If an action is bound to a dpad or dclick action in every profile, action.xr will be XR_NULL_HANDLE
	if (action.xr != XR_NULL_HANDLE) {
		// XrResult ret = xrGetActionStateBoolean(xr_session.get(), getInfo, state);
		// OC_XR_LOG(ret);

		float value = 0;

		XrActionStateFloat fstate = { XR_TYPE_ACTION_STATE_FLOAT };
		XrResult ret;

		if (action.stackedValueExtension != XR_NULL_HANDLE) {
			auto getInfo2 = *getInfo;
			getInfo2.action = action.stackedValueExtension;
			ret = xrGetActionStateFloat(xr_session.get(), &getInfo2, &fstate);
			OC_XR_LOG(ret);

			if (fstate.currentState > 0) {
				value = 1 + fstate.currentState;
			}
		}

		if (value <= 0) { // only query base action if its value will be used to ensure that active/change time are sourced properly
			ret = xrGetActionStateFloat(xr_session.get(), getInfo, &fstate);
			OC_XR_LOG(ret);
			value = fstate.currentState;
		}

		// it would be better to only do this for inputs which actually can use the conversion
		XrBool32& subaction_state = action.analog_to_digital_last_state_subaction[getInfo->subactionPath];
		XrBool32 oldstate = subaction_state;
		if (value >= perProfileData.click_activate_threshold) {
			subaction_state = XR_TRUE;
		}
		if (value <= perProfileData.click_deactivate_threshold) {
			subaction_state = XR_FALSE;
		}
		state->currentState = subaction_state;
		state->changedSinceLastSync = oldstate != subaction_state;
		state->isActive = fstate.isActive;
		state->lastChangeTime = fstate.lastChangeTime;
		state->next = nullptr;

		// actions could be bound to regular buttons and dpad buttons
		if ((perProfileData.dpadBindings.empty() && perProfileData.dclickBindings.empty()) || state->currentState == XR_TRUE)
			return ret;
	}

	bool compoundLastState = false;

	// double clicks: check time between clicks and emulate openvr 'double' inputs
	if (!perProfileData.dclickBindings.empty())
		for (auto& dclick_info : action.perProfileData[profile].dclickBindings) {
			XrActionStateBoolean click_state = { XR_TYPE_ACTION_STATE_BOOLEAN };

			// read state of the source click binding
			XrActionStateGetInfo info2 = *getInfo;
			info2.action = dclick_info.click_action;
			OC_XR_LOG(xrGetActionStateBoolean(xr_session.get(), &info2, &click_state));

			compoundLastState |= dclick_info.last_state;

			if (!click_state.isActive)
				continue;
			state->isActive = XR_TRUE;

			// if the double click has been triggered, hold it triggered until depressed
			if (dclick_info.last_state) {
				if (click_state.currentState) {
					state->currentState = XR_TRUE;
				} else {
					dclick_info.last_state = false;
					state->lastChangeTime = click_state.lastChangeTime;
				}

				continue;
			}

			// check if there has been a *new* button down press
			if (!click_state.changedSinceLastSync || !click_state.currentState)
				continue;

			// Games may read an action more than once per frame (e.g. once per input source); count each press once
			if (click_state.lastChangeTime == dclick_info.last_press_time)
				continue;
			dclick_info.last_press_time = click_state.lastChangeTime;

			if (!dclick_info.first_click_time) {
				// first click, record the timestamp
				dclick_info.first_click_time = click_state.lastChangeTime;
			} else {
				// second click, check if in max time window
				if (click_state.lastChangeTime - dclick_info.first_click_time < DClickBindingInfo::max_dclick_pause) {
					state->currentState = XR_TRUE;
					state->lastChangeTime = click_state.lastChangeTime;

					dclick_info.first_click_time = 0;
					dclick_info.last_state = true;
				} else {
					// missed time window, treat as a first click
					dclick_info.first_click_time = click_state.lastChangeTime;
				}
			}
		}

	// dpad bindings: need to read parent state(s) and fill in state ourselves
	if (!perProfileData.dpadBindings.empty())
		for (auto& [parent_name, dpad_info] : action.perProfileData[profile].dpadBindings) {
			// read state of parent
			XrActionStateVector2f parent_state = { XR_TYPE_ACTION_STATE_VECTOR2F };
			auto iter = DpadBindingInfo::parents.find(parent_name);
			if (iter == DpadBindingInfo::parents.end())
				return XR_ERROR_HANDLE_INVALID; // no parent dpad action: the binding reads as inactive
			XrActionStateGetInfo info2 = *getInfo;
			info2.action = iter->second.vectorAction;
			OC_XR_LOG(xrGetActionStateVector2f(xr_session.get(), &info2, &parent_state));

			compoundLastState |= dpad_info.lastState;
			if (!parent_state.isActive)
				continue;
			state->isActive = XR_TRUE;

			// convert to polar coordinates
			// angle is in radians
			float radius = sqrtf(powf(parent_state.currentState.x, 2) + powf(parent_state.currentState.y, 2));
			float angle = atan2f(parent_state.currentState.y, parent_state.currentState.x);

			// note: since the range of atan2 is (-pi, pi), the east and west directions will have points between the negative and positive portions (if you think of the dpad as a circle)
			bool within_bounds = false;
			switch (dpad_info.direction) {
			case DpadBindingInfo::Direction::CENTER: {
				within_bounds = (radius <= DpadBindingInfo::dpadDeadzoneRadius);
				break;
			}
			case DpadBindingInfo::Direction::NORTH: {
				within_bounds = (radius > DpadBindingInfo::dpadDeadzoneRadius) && (angle > DpadBindingInfo::angle45deg && angle <= DpadBindingInfo::angle135deg);
				break;
			}
			case DpadBindingInfo::Direction::WEST: {
				within_bounds = (radius > DpadBindingInfo::dpadDeadzoneRadius) && (angle > DpadBindingInfo::angle135deg || angle <= -DpadBindingInfo::angle135deg);
				break;
			}
			case DpadBindingInfo::Direction::SOUTH: {
				within_bounds = (radius > DpadBindingInfo::dpadDeadzoneRadius) && (angle > -DpadBindingInfo::angle135deg && angle <= -DpadBindingInfo::angle45deg);
				break;
			}
			case DpadBindingInfo::Direction::EAST: {
				within_bounds = (radius > DpadBindingInfo::dpadDeadzoneRadius) && (angle > -DpadBindingInfo::angle45deg && angle <= DpadBindingInfo::angle45deg);
				break;
			}
			}

			bool active;
			if (dpad_info.click) {
				if (dpad_info.customClickThresholds) {
					XrActionStateFloat click_state{ XR_TYPE_ACTION_STATE_FLOAT };
					info2.action = iter->second.clickAction;
					OC_XR_LOG(xrGetActionStateFloat(xr_session.get(), &info2, &click_state));
					if (dpad_info.lastState) {
						active = click_state.currentState > DpadBindingInfo::clickDeactivateThreshold;
					} else {
						active = click_state.currentState > DpadBindingInfo::clickActivateThreshold;
					}
				} else {
					XrActionStateBoolean click_state{ XR_TYPE_ACTION_STATE_BOOLEAN };
					info2.action = iter->second.clickAction;
					OC_XR_LOG(xrGetActionStateBoolean(xr_session.get(), &info2, &click_state));
					active = click_state.currentState;
				}
			} else if (iter->second.touchAction != XR_NULL_HANDLE) {
				XrActionStateBoolean touch_state{ XR_TYPE_ACTION_STATE_BOOLEAN };
				info2.action = iter->second.touchAction;
				OC_XR_LOG(xrGetActionStateBoolean(xr_session.get(), &info2, &touch_state));
				active = touch_state.currentState;
			} else {
				// touch dpad, but our dpad parent doesn't have a touch input
				active = true;
			}
			bool currentState = active && within_bounds;
			if (currentState)
				state->currentState = XR_TRUE;

			if (currentState != dpad_info.lastState) {
				state->lastChangeTime = parent_state.lastChangeTime;
				dpad_info.lastState = currentState;

				if (currentState && dpad_info.triggerHapticOnClick) {
					for (auto& otherAction : actions.GetItems()) {
						if (!otherAction->haptic || otherAction->xr == XR_NULL_HANDLE)
							continue;
						XrHapticActionInfo hapticInfo = { XR_TYPE_HAPTIC_ACTION_INFO };
						hapticInfo.action = otherAction->xr;
						hapticInfo.subactionPath = getInfo->subactionPath;
						XrHapticVibration vibration = { XR_TYPE_HAPTIC_VIBRATION };
						vibration.frequency = XR_FREQUENCY_UNSPECIFIED;
						vibration.duration = XR_MIN_HAPTIC_DURATION;
						vibration.amplitude = 0.25f;
						OOVR_FAILED_XR_SOFT_ABORT(xrApplyHapticFeedback(xr_session.get(), &hapticInfo, (XrHapticBaseHeader*)&vibration));
						break;
					}
				}
			}
		}
	state->changedSinceLastSync = compoundLastState != (bool)state->currentState;
	return XR_SUCCESS;
}

EVRInputError BaseInput::GetDigitalActionData(VRActionHandle_t action, InputDigitalActionData_t* pActionData, uint32_t unActionDataSize,
    VRInputValueHandle_t ulRestrictToDevice)
{
	// Zeroed before the handle is looked up: an action left out of the manifest answers InvalidHandle, and inactive
	if (pActionData && unActionDataSize == sizeof(*pActionData))
		ZeroMemory(pActionData, unActionDataSize);
	GET_ACTION_FROM_HANDLE(act, action);

	// A null or wrong-sized struct is an argument error, as SteamVR answers (VRInputError_InvalidParam)
	if (!pActionData || unActionDataSize != sizeof(*pActionData))
		return VRInputError_InvalidParam;

	XrActionStateGetInfo getInfo = { XR_TYPE_ACTION_STATE_GET_INFO };
	getInfo.action = act->xr;

	// Unfortunately to implement activeOrigin we have to loop through and query each action state
	for (size_t i = 0; i < allSubactionPaths.size(); i++) {
		XrPath subactionPath = allSubactionPaths[i];

		if (!checkRestrictToDevice(ulRestrictToDevice, subactionPath))
			continue;

		getInfo.subactionPath = subactionPath;
		XrActionStateBoolean state = { XR_TYPE_ACTION_STATE_BOOLEAN };
		OC_XR_LOG(getBooleanOrDpadData(handInteractionProfiles[i], *act, &getInfo, &state));

		// If the subaction isn't set, or it was set but not active, or it was set
		// but the state was false and it's not now, then override it.
		const bool nowActive = state.isActive && !pActionData->bActive;
		const bool nowSet = state.currentState && !pActionData->bState;
		if (!nowActive && !nowSet)
			continue;

		pActionData->bState = state.currentState;
		pActionData->bActive = state.isActive;
		pActionData->bChanged = state.changedSinceLastSync;
		// TODO implement fUpdateTime
		pActionData->activeOrigin = activeOriginFromSubaction(act, allSubactionPathNames[i].c_str());
	}

	if (pActionData->bActive) {
		if (syncSerialDigital > act->previousSerial) {
			const float fState = pActionData->bState ? 1.0f : 0.0f;
			act->deltaState.x = fState - act->previousState.x;
			act->previousState.x = fState;
			act->previousSerial = syncSerialDigital;
		}
		pActionData->bChanged |= act->deltaState.x != 0.0;
	}

	// Note it's possible we didn't set any output if this action isn't bound to anything, just leave the
	//  struct at it's default values.

	return VRInputError_None;
}

EVRInputError BaseInput::GetAnalogActionData(VRActionHandle_t action, InputAnalogActionData_t* pActionData, uint32_t unActionDataSize,
    VRInputValueHandle_t ulRestrictToDevice)
{
	// Zeroed before the handle is looked up: an action left out of the manifest answers InvalidHandle, and inactive
	if (pActionData && unActionDataSize == sizeof(*pActionData))
		ZeroMemory(pActionData, unActionDataSize);
	GET_ACTION_FROM_HANDLE(act, action);
	// A null or wrong-sized struct is an argument error, as SteamVR answers (VRInputError_InvalidParam)
	if (!pActionData || unActionDataSize != sizeof(*pActionData))
		return VRInputError_InvalidParam;

	XrActionStateGetInfo getInfo = { XR_TYPE_ACTION_STATE_GET_INFO };
	getInfo.action = act->xr;

	// Only return the input with the greatest magnitude
	float maxLengthSq = 0;
	bool foundValidInput = false;

	for (size_t i = 0; i < allSubactionPaths.size(); i++) {
		XrPath subactionPath = allSubactionPaths[i];
		if (!checkRestrictToDevice(ulRestrictToDevice, subactionPath))
			continue;

		getInfo.subactionPath = subactionPath;

		auto profile = handInteractionProfiles[i];
		const auto& action_per_profile_data = act->perProfileData.contains(profile) ? act->perProfileData[profile] : Action::defaultActionData;
		switch (act->type) {
		case ActionType::Vector1: {
			XrActionStateFloat state = { XR_TYPE_ACTION_STATE_FLOAT };
			OC_XR_LOG(xrGetActionStateFloat(xr_session.get(), &getInfo, &state));

			if (!state.isActive)
				continue;

			float length = fabs(state.currentState);
			if (length < action_per_profile_data.deadzone)
				continue;

			// Avoid division by zero by checking if maxzone and deadzone are equal
			if (std::abs(action_per_profile_data.maxzone - action_per_profile_data.deadzone) < std::numeric_limits<float>::epsilon())
				continue;

			length = (length - action_per_profile_data.deadzone) / (action_per_profile_data.maxzone - action_per_profile_data.deadzone);
			length = std::min(1.0f, length); // Clamp to maximum of 1.0

			float currentState = state.currentState > 0.f ? length : -length;
			float lengthSq = currentState * currentState;

			if (lengthSq < maxLengthSq)
				continue;

			maxLengthSq = lengthSq;
			foundValidInput = true;

			pActionData->x = currentState;
			pActionData->y = 0;
			pActionData->z = 0;
			pActionData->bActive = state.isActive;
			pActionData->activeOrigin = activeOriginFromSubaction(act, allSubactionPathNames[i].c_str());

			if (syncSerial > act->previousSerial) {
				act->deltaState.x = state.currentState - act->previousState.x;
				act->previousState.x = state.currentState;
				act->previousSerial = syncSerial;
			}

			pActionData->deltaX = act->deltaState.x;
			break;
		}
		case ActionType::Vector2: {
			XrActionStateVector2f state = { XR_TYPE_ACTION_STATE_VECTOR2F };
			OC_XR_LOG(xrGetActionStateVector2f(xr_session.get(), &getInfo, &state));

			if (!state.isActive)
				continue;

			float lengthSq = state.currentState.x * state.currentState.x + state.currentState.y * state.currentState.y;
			float length = std::sqrt(lengthSq);

			if (length < action_per_profile_data.deadzone)
				continue;

			// Avoid division by zero by checking if maxzone and deadzone are equal
			if (std::abs(action_per_profile_data.maxzone - action_per_profile_data.deadzone) < std::numeric_limits<float>::epsilon())
				continue;

			float normalizedLength = (length - action_per_profile_data.deadzone) / (action_per_profile_data.maxzone - action_per_profile_data.deadzone);
			normalizedLength = std::min(1.0f, normalizedLength);

			// Calculate normalized vector components
			float scale;
			if (length < std::numeric_limits<float>::epsilon()) {
				scale = 0.f;
			} else {
				scale = normalizedLength / length;
			}
			XrVector2f normalizedState = {
				state.currentState.x * scale,
				state.currentState.y * scale
			};

			float normalizedLengthSq = normalizedState.x * normalizedState.x + normalizedState.y * normalizedState.y;

			if (normalizedLengthSq < maxLengthSq)
				continue;

			maxLengthSq = normalizedLengthSq;
			foundValidInput = true;

			pActionData->x = normalizedState.x;
			pActionData->y = normalizedState.y;
			pActionData->z = 0;
			pActionData->bActive = state.isActive;
			pActionData->activeOrigin = activeOriginFromSubaction(act, allSubactionPathNames[i].c_str());

			if (syncSerial > act->previousSerial) {
				act->deltaState.x = state.currentState.x - act->previousState.x;
				act->deltaState.y = state.currentState.y - act->previousState.y;
				act->previousState.x = state.currentState.x;
				act->previousState.y = state.currentState.y;
				act->previousSerial = syncSerial;
			}

			pActionData->deltaX = act->deltaState.x;
			pActionData->deltaY = act->deltaState.y;
			break;
		}
		case ActionType::Vector3:
			// For now: a vector3 action reads as inactive
			OC_UNSUPPORTED(VRInputError_None, "Input type vector3 unsupported: %s", act->fullName.c_str());
		default:
			OC_UNSUPPORTED(VRInputError_WrongType, "Invalid action type %d for action %s",
			    static_cast<int>(act->type), act->fullName.c_str());
		}
	}

	if (!foundValidInput) {
		pActionData->bActive = false;
	}

	return VRInputError_None;
}

EVRInputError BaseInput::GetPoseActionData(VRActionHandle_t action, ETrackingUniverseOrigin eOrigin, float fPredictedSecondsFromNow,
    InputPoseActionData_t* pActionData, uint32_t unActionDataSize, VRInputValueHandle_t ulRestrictToDevice)
{
	// Zeroed before the handle is looked up: an action left out of the manifest answers InvalidHandle, and inactive
	if (pActionData && unActionDataSize == sizeof(*pActionData))
		ZeroMemory(pActionData, unActionDataSize);
	GET_ACTION_FROM_HANDLE(act, action);

	// A null or wrong-sized struct is an argument error, as SteamVR answers (VRInputError_InvalidParam)
	if (!pActionData || unActionDataSize != sizeof(*pActionData))
		return VRInputError_InvalidParam;

	if (act->type == ActionType::Skeleton) {
		std::shared_ptr<ITrackedDevice> dev = BackendManager::Instance().GetDeviceByHand(act->skeletalHand);
		if (!dev)
			return vr::VRInputError_InvalidDevice;

		dev->GetPose(eOrigin, &pActionData->pose, ETrackingStateType::TrackingStateType_Now);
		pActionData->bActive = pActionData->pose.bPoseIsValid && pActionData->pose.bDeviceIsConnected;
		pActionData->activeOrigin = vr::k_ulInvalidInputValueHandle; // TODO implement activeOrigin

		return vr::VRInputError_None;
	}

	if (act->type != ActionType::Pose)
		OC_UNSUPPORTED(VRInputError_WrongType, "Invalid action type %d for action %s", static_cast<int>(act->type), act->fullName.c_str());

	const InputValueHandle* restrictToDevice = nullptr;
	if (ulRestrictToDevice) {
		restrictToDevice = cast_IVH(ulRestrictToDevice);
		if (!restrictToDevice)
			return VRInputError_InvalidHandle;
	}

	for (int handNum = 0; handNum < 2; handNum++) {
		// Check this hand is permitted
		ITrackedDevice::TrackedDeviceType handType = handNum == 0 ? ITrackedDevice::HAND_LEFT : ITrackedDevice::HAND_RIGHT;
		InputSource sourceType = handNum == 0 ? InputSource::HAND_LEFT : InputSource::HAND_RIGHT;

		if (restrictToDevice && restrictToDevice->type != sourceType) {
			continue;
		}

		// Find the device for this hand, and look up it's interaction profile
		std::shared_ptr<ITrackedDevice> dev = BackendManager::Instance().GetDeviceByHand(handType);
		if (!dev)
			continue;

		const InteractionProfile* profile = dev->GetInteractionProfile();
		if (!profile)
			continue;

		const auto& bindings = handType == ITrackedDevice::HAND_LEFT ? act->poseBindingsLeft : act->poseBindingsRight;
		const auto bindingIter = bindings.find(profile);
		if (bindingIter == bindings.end())
			continue;
		const PoseBindingInfo& binding = bindingIter->second;

		// TODO use predicted time

		// Regardless of whether valid data is available, this action is bound
		pActionData->bActive = true;

		// TODO make sure this is correct
		pActionData->activeOrigin = activeOriginFromSubaction(act, allSubactionPathNames[handNum].c_str());

		vr::TrackedDevicePose_t rawPose = {};
		dev->GetPose(eOrigin, &rawPose, TrackingStateType_Now);

		if (rawPose.bPoseIsValid) {
			glm::mat4 handMat = S2G_m34(rawPose.mDeviceToAbsoluteTracking);

			const char* componentName = nullptr;
			switch (binding.point) {
			case PoseBindingPoint::RAW:
			case PoseBindingPoint::BODY:
			case PoseBindingPoint::GDC2015:
				// No component name
				break;
			case PoseBindingPoint::GRIP:
				componentName = "grip";
				break;
			case PoseBindingPoint::BASE:
				componentName = "base";
				break;
			case PoseBindingPoint::HANDGRIP:
				componentName = "handgrip";
				break;
			case PoseBindingPoint::TIP:
				componentName = "tip";
				break;
			}

			// Add the component transform, if required
			OOVR_RenderModel_ComponentState_t state = {};
			bool hasTransform = false;
			glm::mat4 transform = glm::identity<glm::mat4>();
			if (componentName) {
				hasTransform = GetBaseRenderModels()->TryGetComponentState(handType, componentName, &state);
			}
			if (hasTransform) {
				transform = S2G_m34(state.mTrackingToComponentLocal);

				handMat = handMat * transform;
			}

			// Store the final hand matrix
			pActionData->pose.mDeviceToAbsoluteTracking = G2S_m34(handMat);
			pActionData->pose.bPoseIsValid = true;
			pActionData->pose.bDeviceIsConnected = true;
			pActionData->pose.eTrackingResult = rawPose.eTrackingResult;

			// The velocity stays in the original coordinate system, no translation required
			pActionData->pose.vVelocity = rawPose.vVelocity;

			// TODO what about angular velocity?
			pActionData->pose.vAngularVelocity = rawPose.vAngularVelocity;
		}

		// TODO implement the deltas

		// Stop as soon as we find the first available input (and a disconnected controller is
		// fine to count as available - that's probably not how it should be, but it'd be a
		// pain to do it properly)
		break;
	}

	return VRInputError_None;
}

EVRInputError BaseInput::GetPoseActionDataRelativeToNow(VRActionHandle_t action, ETrackingUniverseOrigin eOrigin, float fPredictedSecondsFromNow, InputPoseActionData_t* pActionData, uint32_t unActionDataSize, VRInputValueHandle_t ulRestrictToDevice)
{
	// Same function, different name - the 'RelativeToNow' suffix was added when GetPoseActionDataForNextFrame was added
	return GetPoseActionData(action, eOrigin, fPredictedSecondsFromNow, pActionData, unActionDataSize, ulRestrictToDevice);
}
EVRInputError BaseInput::GetPoseActionDataForNextFrame(VRActionHandle_t action, ETrackingUniverseOrigin eOrigin, InputPoseActionData_t* pActionData, uint32_t unActionDataSize, VRInputValueHandle_t ulRestrictToDevice)
{
	return GetPoseActionData(action, eOrigin, 0, pActionData, unActionDataSize, ulRestrictToDevice);
}
EVRInputError BaseInput::GetSkeletalActionData(VRActionHandle_t action, InputSkeletalActionData_t* pActionData, uint32_t unActionDataSize,
    VRInputValueHandle_t ulRestrictToDevice)
{
	// This is the old version of the function, the new one doesn't have the device restriction
	// Hopefully noone depends on this and we can just ignore it
	if (ulRestrictToDevice != vr::k_ulInvalidInputValueHandle) {
		OOVR_SOFT_ABORT("Old GetSkeletalActionData device restrictions not supported");
	}

	return GetSkeletalActionData(action, pActionData, unActionDataSize);
}
EVRInputError BaseInput::GetSkeletalActionData(VRActionHandle_t actionHandle, InputSkeletalActionData_t* out, uint32_t unActionDataSize)
{
	// Make sure the target struct is the right size, in case it grows in the future
	// Note: CVRInput_004 has a manual override for this in CVRInput.cpp to deal with an old struct version, it
	// sets unActionDataSize to what we're expecting so we don't have to handle that here.
	// A null or wrong-sized struct is an argument error, as SteamVR answers (VRInputError_InvalidParam)
	if (!out || unActionDataSize != sizeof(*out))
		return VRInputError_InvalidParam;

	// Zero out the output data, leaving:
	// bActive = false
	// activeOrigin = vr::k_ulInvalidActionHandle
	// (before the handle is looked up: an action left out of the manifest answers InvalidHandle, and inactive)
	ZeroMemory(out, unActionDataSize);

	GET_ACTION_FROM_HANDLE(action, actionHandle);

	// Find the active origin for this hand
	std::string originPath;
	if (action->skeletalHand == ITrackedDevice::HAND_LEFT)
		originPath = "/user/hand/left";
	else
		originPath = "/user/hand/right";
	if (GetInputSourceHandle(originPath.c_str(), &out->activeOrigin) != vr::VRInputError_None)
		out->activeOrigin = vr::k_ulInvalidInputValueHandle;

	out->bActive = ivhToDev(out->activeOrigin) != nullptr;
	return vr::VRInputError_None;
}

EVRInputError BaseInput::GetDominantHand(vr::ETrackedControllerRole* peDominantHand)
{
	// The API documentation says we need allowSetDominantHand for this, but that
	// seems like a mistake (i.e. that comment was probably meant to be on
	// SetDominantHand), so I'll just allow it always.
	if (!peDominantHand)
		return VRInputError_InvalidParam;
	*peDominantHand = dominantHand;
	return VRInputError_None;
}
EVRInputError BaseInput::SetDominantHand(vr::ETrackedControllerRole eDominantHand)
{
	if (allowSetDominantHand) {
		dominantHand = eDominantHand;
		return VRInputError_None;
	} else {
		return VRInputError_PermissionDenied;
	}
}
EVRInputError BaseInput::GetBoneCount(VRActionHandle_t action, uint32_t* pBoneCount)
{
	// Maybe we should check the action?
	if (!pBoneCount)
		return VRInputError_InvalidParam;
	*pBoneCount = (int)eBone_Count;
	return vr::VRInputError_None;
}
EVRInputError BaseInput::GetBoneHierarchy(VRActionHandle_t action, VR_ARRAY_COUNT(unIndexArayCount) BoneIndex_t* pParentIndices, uint32_t unIndexArayCount)
{
	GET_ACTION_FROM_HANDLE(act, action);
	if ((int)act->skeletalHand >= 2)
		return VRInputError_InvalidSkeleton;
	if (!pParentIndices || unIndexArayCount < eBone_Count)
		return VRInputError_BufferTooSmall;
	for (BoneIndex_t i = 0; i < eBone_Count; i++)
		pParentIndices[i] = HandBoneParent(i);
	return VRInputError_None;
}
EVRInputError BaseInput::GetBoneName(VRActionHandle_t action, BoneIndex_t nBoneIndex, VR_OUT_STRING() char* pchBoneName, uint32_t unNameBufferSize)
{
	if (pchBoneName && unNameBufferSize)
		pchBoneName[0] = 0;
	GET_ACTION_FROM_HANDLE(act, action);
	if ((int)act->skeletalHand >= 2)
		return VRInputError_InvalidSkeleton;
	if (nBoneIndex < 0 || nBoneIndex >= eBone_Count)
		return VRInputError_InvalidBoneIndex;
	std::string name = HandBoneName(nBoneIndex, act->skeletalHand == ITrackedDevice::HAND_RIGHT);
	if (!pchBoneName || unNameBufferSize < name.size() + 1)
		return VRInputError_BufferTooSmall;
	memcpy(pchBoneName, name.c_str(), name.size() + 1);
	return VRInputError_None;
}
EVRInputError BaseInput::GetSkeletalReferenceTransforms(VRActionHandle_t actionHandle, EVRSkeletalTransformSpace eTransformSpace, EVRSkeletalReferencePose eReferencePose, VR_ARRAY_COUNT(unTransformArrayCount) VRBoneTransform_t* pTransformArray, uint32_t unTransformArrayCount)
{
	GET_ACTION_FROM_HANDLE(act, actionHandle);

	std::span<VRBoneTransform_t> out(pTransformArray, unTransformArrayCount);

	// Find the interaction profile to retrieve hand pose
	std::shared_ptr<ITrackedDevice> dev = BackendManager::Instance().GetDeviceByHand(act->skeletalHand);
	if (!dev)
		return vr::VRInputError_InvalidDevice;

	const InteractionProfile* profile = dev->GetInteractionProfile();
	if (!profile)
		return vr::VRInputError_InvalidDevice;

	const std::optional<BoneArray> pose = profile->GetSkeletalReferencePose(act->skeletalHand, eReferencePose);
	if (!pose.has_value()) {
		OOVR_LOGF("WARNING: Couldn't find reference pose: %d, %d", act->skeletalHand, eReferencePose);
		return vr::VRInputError_InvalidParam;
	}

	std::copy(std::begin(pose.value()), std::end(pose.value()), out.begin());

	if (eTransformSpace == EVRSkeletalTransformSpace::VRSkeletalTransformSpace_Model)
		ParentSpaceSkeletonToModelSpace(pTransformArray);

	return vr::VRInputError_None;
}
EVRInputError BaseInput::GetSkeletalTrackingLevel(VRActionHandle_t action, EVRSkeletalTrackingLevel* pSkeletalTrackingLevel)
{
	GET_ACTION_FROM_HANDLE(act, action);

	if (act->skeletalHand == ITrackedDevice::HAND_NONE)
		return vr::VRInputError_InvalidHandle;

	std::shared_ptr<ITrackedDevice> dev = BackendManager::Instance().GetDeviceByHand(act->skeletalHand);
	if (!dev)
		return vr::VRInputError_InvalidDevice;

	const InteractionProfile* profile = dev->GetInteractionProfile();

	// HACK until we have hand tracking data source in monado, i'm hardcoding this here so games won't think the index is a proper hand.
	if (profile && profile->GetPath() == "/interaction_profiles/valve/index_controller") {
		*pSkeletalTrackingLevel = vr::VRSkeletalTracking_Partial;
		return vr::VRInputError_None;
	}

	if (dev->IsHandTrackingValid()) {
		*pSkeletalTrackingLevel = vr::VRSkeletalTracking_Full;
		return vr::VRInputError_None;
	}

	if (profile) {
		std::optional<vr::EVRSkeletalTrackingLevel> level = profile->GetOpenVRTrackinglevel();
		if (level.has_value()) {
			*pSkeletalTrackingLevel = static_cast<vr::EVRSkeletalTrackingLevel>(level.value());
			return vr::VRInputError_None;
		} else {
			*pSkeletalTrackingLevel = vr::VRSkeletalTracking_Estimated;
		}
	} else {
		*pSkeletalTrackingLevel = vr::VRSkeletalTracking_Estimated;
	}

	return vr::VRInputError_None;
}
EVRInputError BaseInput::GetSkeletalBoneData(VRActionHandle_t action, EVRSkeletalTransformSpace eTransformSpace,
    EVRSkeletalMotionRange eMotionRange, VR_ARRAY_COUNT(unTransformArrayCount) VRBoneTransform_t* pTransformArray,
    uint32_t unTransformArrayCount, VRInputValueHandle_t ulRestrictToDevice)
{
	// This is the old version of the function, the new one doesn't have the device restriction
	// Hopefully noone depends on this and we can just ignore it
	if (ulRestrictToDevice != vr::k_ulInvalidInputValueHandle) {
		OOVR_SOFT_ABORT("Old skeletal input device restrictions not supported");
	}

	return GetSkeletalBoneData(action, eTransformSpace, eMotionRange, pTransformArray, unTransformArrayCount);
}

EVRInputError BaseInput::GetSkeletalBoneData(VRActionHandle_t actionHandle, EVRSkeletalTransformSpace eTransformSpace,
    EVRSkeletalMotionRange eMotionRange, VR_ARRAY_COUNT(unTransformArrayCount) VRBoneTransform_t* pTransformArray, uint32_t unTransformArrayCount)
{
	ZeroMemory(pTransformArray, sizeof(VRBoneTransform_t) * unTransformArrayCount);
	GET_ACTION_FROM_HANDLE(action, actionHandle);

	// FIXME remove
	// Hand labs is in parent transform space, and usually 'with controller' motion range
	// OOVR_LOGF("skeletal data %d %d", eTransformSpace, eMotionRange);

	// Check for the right number of bones (and a hand), as SteamVR answers a wrong count
	if (!pTransformArray || unTransformArrayCount != 31)
		return vr::VRInputError_InvalidParam;

	const auto hand = action->skeletalHand;
	if (static_cast<int>(hand) >= 2)
		return vr::VRInputError_InvalidSkeleton;

	std::shared_ptr<ITrackedDevice> dev = BackendManager::Instance().GetDeviceByHand(hand);
	if (!dev)
		return vr::VRInputError_InvalidDevice;

	if (!xr_gbl || !xr_gbl->handTrackingProperties.supportsHandTracking) {
		dev->SetHandTrackingValid(false);
		return getEstimatedBoneData(hand, eTransformSpace, eMotionRange, std::span<VRBoneTransform_t, eBone_Count>(pTransformArray, eBone_Count));
	}

	bool usingFakePose = dev->IsPoseFromHandTracking();

	XrHandJointsLocateInfoEXT locateInfo = { XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT };
	// If the pose is fake that means we don't have controllers the hand must be located relative to floor
	locateInfo.baseSpace = usingFakePose ? xr_gbl->floorSpace : legacyControllers[static_cast<int>(hand)].gripPoseSpace;
	locateInfo.time = xr_gbl->GetBestTime();

	XrHandJointLocationsEXT locations = { XR_TYPE_HAND_JOINT_LOCATIONS_EXT };
	locations.jointCount = XR_HAND_JOINT_COUNT_EXT;
	std::vector<XrHandJointLocationEXT> jointLocations(locations.jointCount);
	locations.jointLocations = jointLocations.data();

	OC_XR_LOG(xr_ext->xrLocateHandJointsEXT(handTrackers[(int)action->skeletalHand], &locateInfo, &locations));

	if (!locations.isActive) {
		// Fallback to estimated bone data (e.g. for controllers)
		dev->SetHandTrackingValid(false);
		return getEstimatedBoneData(hand, eTransformSpace, eMotionRange, std::span<VRBoneTransform_t, eBone_Count>(pTransformArray, eBone_Count));
	}

	bool isRight = (action->skeletalHand == ITrackedDevice::HAND_RIGHT);
	const InteractionProfile* profile = dev->GetInteractionProfile();

	// Retrieve the appropriate hand transform to account for the SteamVR pose being different from OpenXR grip
	glm::mat4 transform = profile->GetGripToSteamVRTransform(hand);

	if (!XrHandJointsToSkeleton(jointLocations, isRight, pTransformArray, transform))
		return getEstimatedBoneData(hand, eTransformSpace, eMotionRange, std::span<VRBoneTransform_t, eBone_Count>(pTransformArray, eBone_Count));

	// Without a controller we get a hand positioned relative to floor space, change that to be relative to the fake controller pose
	if (usingFakePose) {
		vr::VRBoneTransform_t& wrist = pTransformArray[eBone_Wrist];
		if (isRight) {
			wrist.position = { 0.034038f, 0.036503f, 0.164722f, 1.000000f };
			wrist.orientation = { -0.055147f, -0.078608f, 0.920279f, -0.379296f };
		} else {
			wrist.position = { -0.034038f, 0.036503f, 0.164722f, 1.000000f };
			wrist.orientation = { -0.055147f, -0.078608f, -0.920279f, 0.379296f };
		}
	}

	if (eTransformSpace == EVRSkeletalTransformSpace::VRSkeletalTransformSpace_Model)
		ParentSpaceSkeletonToModelSpace(pTransformArray);

	dev->SetHandTrackingValid(true);

	// For now, just return with non-active data
	return vr::VRInputError_None;
}
EVRInputError BaseInput::GetSkeletalSummaryData(VRActionHandle_t actionHandle, EVRSummaryType eSummaryType, VRSkeletalSummaryData_t* pSkeletalSummaryData)
{
	GET_ACTION_FROM_HANDLE(action, actionHandle);

	if (!pSkeletalSummaryData)
		return VRInputError_InvalidParam;
	ZeroMemory(pSkeletalSummaryData, sizeof(VRSkeletalSummaryData_t));

	if (xr_gbl && xr_gbl->handTrackingProperties.supportsHandTracking) {
		return getRealSkeletalSummary(action->skeletalHand, pSkeletalSummaryData);
	}

	return getEstimatedSkeletalSummary(action->skeletalHand, pSkeletalSummaryData);
}

EVRInputError BaseInput::getRealSkeletalSummary(ITrackedDevice::TrackedDeviceType hand, VRSkeletalSummaryData_t* pSkeletalSummaryData)
{
	if (!xr_gbl)
		return getEstimatedSkeletalSummary(hand, pSkeletalSummaryData);
	XrHandJointsLocateInfoEXT locateInfo = { XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT };
	locateInfo.baseSpace = xr_gbl->floorSpace;
	locateInfo.time = xr_gbl->GetBestTime();

	XrHandJointLocationsEXT locations = { XR_TYPE_HAND_JOINT_LOCATIONS_EXT };
	locations.jointCount = XR_HAND_JOINT_COUNT_EXT;
	std::vector<XrHandJointLocationEXT> jointLocations(locations.jointCount);
	locations.jointLocations = jointLocations.data();

	OC_XR_LOG(xr_ext->xrLocateHandJointsEXT(handTrackers[hand], &locateInfo, &locations));

	if (!locations.isActive) {
		// Fallback to estimated skeletal summary data (e.g. for controllers)
		return getEstimatedSkeletalSummary(hand, pSkeletalSummaryData);
	}

	for (int i = 0; i < 5; ++i) {
		XrHandJointLocationEXT metacarpal, proximal, tip;

		switch (i) {
		case 0: // thumb
			metacarpal = jointLocations[XR_HAND_JOINT_THUMB_METACARPAL_EXT];
			proximal = jointLocations[XR_HAND_JOINT_THUMB_PROXIMAL_EXT];
			tip = jointLocations[XR_HAND_JOINT_THUMB_TIP_EXT];
			break;
		case 1: // index
			metacarpal = jointLocations[XR_HAND_JOINT_INDEX_METACARPAL_EXT];
			proximal = jointLocations[XR_HAND_JOINT_INDEX_PROXIMAL_EXT];
			tip = jointLocations[XR_HAND_JOINT_INDEX_TIP_EXT];
			break;
		case 2: // middle
			metacarpal = jointLocations[XR_HAND_JOINT_MIDDLE_METACARPAL_EXT];
			proximal = jointLocations[XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT];
			tip = jointLocations[XR_HAND_JOINT_MIDDLE_TIP_EXT];
			break;
		case 3: // ring
			metacarpal = jointLocations[XR_HAND_JOINT_RING_METACARPAL_EXT];
			proximal = jointLocations[XR_HAND_JOINT_RING_PROXIMAL_EXT];
			tip = jointLocations[XR_HAND_JOINT_RING_TIP_EXT];
			break;
		case 4: // little
			metacarpal = jointLocations[XR_HAND_JOINT_LITTLE_METACARPAL_EXT];
			proximal = jointLocations[XR_HAND_JOINT_LITTLE_PROXIMAL_EXT];
			tip = jointLocations[XR_HAND_JOINT_LITTLE_TIP_EXT];
			break;
		default:
			break;
		}

		glm::vec3 metacarpalProximalDelta = X2G_v3f(metacarpal.pose.position) - X2G_v3f(proximal.pose.position);
		glm::vec3 tipProximalDelta = X2G_v3f(tip.pose.position) - X2G_v3f(proximal.pose.position);

		float dot = glm::dot(metacarpalProximalDelta, tipProximalDelta);
		float a = glm::length(metacarpalProximalDelta);
		float b = glm::length(tipProximalDelta);
		// dot = a * b * cos(ang)

		float curl;
		if (a == 0.0f || b == 0.0f) {
			// That's probably not meant to happen? But if two joints really do
			// coincide then that probably means the hand is curled up
			curl = 1.0f;
		} else {
			// Find the angle between these three joints
			float angCos = dot / (a * b);
			if (angCos < -1.0f)
				angCos = -1.0f;
			if (angCos > 1.0f)
				angCos = 1.0f;
			float ang = acosf(angCos);
			curl = 1.0f - (ang / std::numbers::pi_v<float>);
		}

		pSkeletalSummaryData->flFingerCurl[i] = curl;
	}

	for (int i = 0; i < 4; ++i) {
		OOVR_LOG_ONCE("Finger splay hardcoded at 0.2");
		pSkeletalSummaryData->flFingerSplay[i] = 0.2f; // TODO
	}

	return vr::VRInputError_None;
}

EVRInputError BaseInput::GetSkeletalSummaryData(VRActionHandle_t action, VRSkeletalSummaryData_t* pSkeletalSummaryData)
{
	return GetSkeletalSummaryData(action, VRSummaryType_FromDevice, pSkeletalSummaryData);
}
EVRInputError BaseInput::GetSkeletalBoneDataCompressed(VRActionHandle_t action, EVRSkeletalTransformSpace eTransformSpace,
    EVRSkeletalMotionRange eMotionRange, VR_OUT_BUFFER_COUNT(unCompressedSize) void* pvCompressedData, uint32_t unCompressedSize,
    uint32_t* punRequiredCompressedSize, VRInputValueHandle_t ulRestrictToDevice)
{
	// OpenComposite's own format (CompressBones): only DecompressSkeletalBoneData here reads it. No bones (a bad
	// action, no hand): no size either, as SteamVR answers
	if (punRequiredCompressedSize)
		*punRequiredCompressedSize = 0;
	BoneArray bones{};
	EVRInputError err = GetSkeletalBoneData(action, eTransformSpace, eMotionRange, bones.data(), (uint32_t)bones.size());
	if (err != VRInputError_None)
		return err;
	if (punRequiredCompressedSize)
		*punRequiredCompressedSize = kCompressedBonesSize;
	if (!pvCompressedData || unCompressedSize < kCompressedBonesSize)
		return VRInputError_BufferTooSmall;
	CompressBones(eTransformSpace, eMotionRange, bones.data(), pvCompressedData, unCompressedSize);
	return VRInputError_None;
}
EVRInputError BaseInput::GetSkeletalBoneDataCompressed(VRActionHandle_t action, EVRSkeletalMotionRange eMotionRange,
    VR_OUT_BUFFER_COUNT(unCompressedSize) void* pvCompressedData, uint32_t unCompressedSize, uint32_t* punRequiredCompressedSize)
{
	// IVRInput_003/004: bones in parent space, as SteamVR gave them then
	return GetSkeletalBoneDataCompressed(action, VRSkeletalTransformSpace_Parent, eMotionRange, pvCompressedData, unCompressedSize,
	    punRequiredCompressedSize, vr::k_ulInvalidInputValueHandle);
}
EVRInputError BaseInput::GetDigitalActionData(VRActionHandle_t action, InputDigitalActionData_t* pActionData, uint32_t unActionDataSize)
{
	return GetDigitalActionData(action, pActionData, unActionDataSize, vr::k_ulInvalidInputValueHandle);
}

EVRInputError BaseInput::GetAnalogActionData(VRActionHandle_t action, InputAnalogActionData_t* pActionData, uint32_t unActionDataSize)
{
	return GetAnalogActionData(action, pActionData, unActionDataSize, vr::k_ulInvalidInputValueHandle);
}

EVRInputError BaseInput::GetPoseActionData(VRActionHandle_t action, ETrackingUniverseOrigin eOrigin, float fPredictedSecondsFromNow,
    InputPoseActionData_t* pActionData, uint32_t unActionDataSize)
{
	return GetPoseActionData(action, eOrigin, fPredictedSecondsFromNow, pActionData, unActionDataSize, vr::k_ulInvalidInputValueHandle);
}

EVRInputError BaseInput::TriggerHapticVibrationAction(VRActionHandle_t action, float fStartSecondsFromNow, float fDurationSeconds,
    float fFrequency, float fAmplitude)
{
	return TriggerHapticVibrationAction(action, fStartSecondsFromNow, fDurationSeconds, fFrequency, fAmplitude, vr::k_ulInvalidInputValueHandle);
}

EVRInputError BaseInput::GetSkeletalActionData(VRActionHandle_t action, EVRSkeletalTransformSpace eBoneParent, float fPredictedSecondsFromNow,
    InputSkeletalActionData_t* pActionData, uint32_t unActionDataSize, VRBoneTransform_t* pTransformArray, uint32_t unTransformArrayCount)
{
	EVRInputError err = GetSkeletalActionData(action, pActionData, unActionDataSize);
	if (err != vr::VRInputError_None)
		return err;
	// A caller that only wants the action data passes no bones
	if (!pTransformArray && unTransformArrayCount == 0)
		return vr::VRInputError_None;
	return GetSkeletalBoneData(action, eBoneParent, vr::VRSkeletalMotionRange_WithController, pTransformArray, unTransformArrayCount);
}

EVRInputError BaseInput::GetSkeletalActionDataCompressed(VRActionHandle_t action, EVRSkeletalTransformSpace eBoneParent,
    float fPredictedSecondsFromNow, void* pvCompressedData, uint32_t unCompressedSize, uint32_t* punRequiredCompressedSize)
{
	return GetSkeletalBoneDataCompressed(action, vr::VRSkeletalMotionRange_WithController, pvCompressedData, unCompressedSize,
	    punRequiredCompressedSize);
}

EVRInputError BaseInput::UncompressSkeletalActionData(void* pvCompressedBuffer, uint32_t unCompressedBufferSize,
    EVRSkeletalTransformSpace* peBoneParent, VRBoneTransform_t* pTransformArray, uint32_t unTransformArrayCount)
{
	return DecompressSkeletalBoneData(pvCompressedBuffer, unCompressedBufferSize, peBoneParent, pTransformArray, unTransformArrayCount);
}

EVRInputError BaseInput::DecompressSkeletalBoneData(void* pvCompressedBuffer, uint32_t unCompressedBufferSize,
    EVRSkeletalTransformSpace* peTransformSpace, VR_ARRAY_COUNT(unTransformArrayCount) VRBoneTransform_t* pTransformArray,
    uint32_t unTransformArrayCount)
{
	// The older call: the bones in the space they were compressed in, and which that was
	if (!pTransformArray || unTransformArrayCount != eBone_Count)
		return VRInputError_InvalidParam;
	return DecompressBones(pvCompressedBuffer, unCompressedBufferSize, std::nullopt, pTransformArray, peTransformSpace);
}
EVRInputError BaseInput::DecompressSkeletalBoneData(const void* pvCompressedBuffer, uint32_t unCompressedBufferSize, EVRSkeletalTransformSpace eTransformSpace,
    VR_ARRAY_COUNT(unTransformArrayCount) VRBoneTransform_t* pTransformArray, uint32_t unTransformArrayCount)
{
	if (!pTransformArray || unTransformArrayCount != eBone_Count)
		return VRInputError_InvalidParam;
	return DecompressBones(pvCompressedBuffer, unCompressedBufferSize, eTransformSpace, pTransformArray);
}

EVRInputError BaseInput::TriggerHapticVibrationAction(VRActionHandle_t action, float fStartSecondsFromNow, float fDurationSeconds,
    float fFrequency, float fAmplitude, VRInputValueHandle_t ulRestrictToDevice)
{
	GET_ACTION_FROM_HANDLE(act, action);

	if (act->type != ActionType::Vibration) {
		OOVR_LOGF("Cannot trigger vibration on non-vibration (type=%d) action '%s'", static_cast<int>(act->type), act->fullName.c_str());
		return VRInputError_WrongType;
	}

	// TODO check the subaction stuff works properly
	XrPath subactionPath = XR_NULL_PATH;
	if (ulRestrictToDevice != vr::k_ulInvalidInputValueHandle) {
		std::shared_ptr<ITrackedDevice> dev = ivhToDev(ulRestrictToDevice);
		if (dev && dev->GetHand() != ITrackedDevice::HAND_NONE) {
			LegacyControllerActions& ctrl = legacyControllers[dev->GetHand()];
			subactionPath = ctrl.handPathXr;
		}
	}

	XrHapticActionInfo info = { XR_TYPE_HAPTIC_ACTION_INFO };
	info.action = act->xr;
	info.subactionPath = subactionPath;

	// TODO implement fStartSecondsFromNow - just assume most games leave it at or very close to zero

	XrHapticVibration vibration = { XR_TYPE_HAPTIC_VIBRATION };
	vibration.frequency = fFrequency == 0.0f ? XR_FREQUENCY_UNSPECIFIED : fFrequency;
	vibration.duration = (int)(fDurationSeconds * 1000000000.0f);
	vibration.amplitude = fAmplitude;

	OC_XR_LOG(xrApplyHapticFeedback(xr_session.get(), &info, (XrHapticBaseHeader*)&vibration));

	return VRInputError_None;
}

EVRInputError BaseInput::GetActionOrigins(VRActionSetHandle_t actionSetHandle, VRActionHandle_t digitalActionHandle,
    VR_ARRAY_COUNT(originOutCount) VRInputValueHandle_t* originsOut, uint32_t originOutCount)
{
	GET_ACTION_SET_FROM_HANDLE(set, actionSetHandle);
	GET_ACTION_FROM_HANDLE(act, digitalActionHandle);

	// TODO find something that passes in non-matching values and see what results it wants, or try it with SteamVR
	if (act->set != set) {
		// The action isn't in that set: no origins for it there
		OOVR_LOGF("GetActionOrigins: set mismatch %s vs %s", set->name.c_str(), act->fullName.c_str());
		return VRInputError_InvalidHandle;
	}

	if (act->type == ActionType::Skeleton || act->type == ActionType::Pose) {
		// TODO what should this do?
		OOVR_SOFT_ABORT("Skeleton and pose action origins not implemented, returning error");
		return VRInputError_InvalidHandle;
	}

	if (!originsOut && originOutCount)
		return VRInputError_InvalidParam;
	if (originsOut)
		ZeroMemory(originsOut, originOutCount * sizeof(*originsOut));

	std::set<std::string> sources;
	XrBoundSourcesForActionEnumerateInfo info = { XR_TYPE_BOUND_SOURCES_FOR_ACTION_ENUMERATE_INFO };
	info.action = act->xr;

	// 20 will be more than enough, saves a second call
	XrPath tmp[20];
	uint32_t count = 0;
	// On failure (or a count beyond tmp, from SIZE_INSUFFICIENT) the sources are none, or tmp's worth
	XrResult enumResult = xrEnumerateBoundSourcesForAction(xr_session.get(), &info, std::size(tmp), &count, tmp);
	if (XR_FAILED(enumResult)) {
		OOVR_LOG_ONCE("GetActionOrigins: xrEnumerateBoundSourcesForAction failed");
		count = 0;
	}
	count = std::min<uint32_t>(count, (uint32_t)std::size(tmp));

	// Now for each source find the /user/hand/abc substring that it starts with
	char buff[XR_MAX_PATH_LENGTH + 1] = {};
	for (uint32_t i = 0; i < count; i++) {
		uint32_t len = 0;
		if (XR_FAILED(xrPathToString(xr_instance, tmp[i], XR_MAX_PATH_LENGTH, &len, buff)) || len == 0)
			continue;

		// len counts the terminator
		std::string path(buff, strnlen(buff, std::min<size_t>(len, XR_MAX_PATH_LENGTH)));
		size_t endOfHandPos = path.find('/', strlen("/user/hand/") + 1);
		if (endOfHandPos != std::string::npos)
			path.erase(endOfHandPos);
		sources.insert(std::move(path));
	}

	// Copy out the sources
	uint32_t i = 0;
	for (const std::string& path : sources) {
		if (i >= originOutCount)
			return vr::VRInputError_MaxCapacityReached; // TODO check this is correct
		if (GetInputSourceHandle(path.c_str(), &originsOut[i]) != vr::VRInputError_None)
			continue;
		i++;
	}

	return VRInputError_None;
}

EVRInputError BaseInput::GetOriginLocalizedName(VRInputValueHandle_t origin, VR_OUT_STRING() char* pchNameArray, uint32_t unNameArraySize)
{
	return GetOriginLocalizedName(origin, pchNameArray, unNameArraySize, VRInputString_All);
}

EVRInputError BaseInput::GetOriginLocalizedName(VRInputValueHandle_t origin, VR_OUT_STRING() char* pchNameArray, uint32_t unNameArraySize,
    uint64_t unStringSectionsToInclude)
{
	if (origin == vr::k_ulInvalidInputValueHandle)
		return vr::VRInputError_InvalidHandle;

	std::shared_ptr<ITrackedDevice> dev = ivhToDev(origin);

	if (!dev)
		return VRInputError_InvalidHandle;

	if (!pchNameArray || unNameArraySize == 0)
		return VRInputError_MaxCapacityReached;

	std::string name;

	if (unStringSectionsToInclude & VRInputString_Hand) {
		switch (dev->GetHand()) {
		case ITrackedDevice::HAND_LEFT:
			name += "Left Hand ";
			break;
		case ITrackedDevice::HAND_RIGHT:
			name += "Right Hand ";
			break;
		default:
			name += "Unknown Hand ";
			break;
		}
	}

	if (unStringSectionsToInclude & VRInputString_ControllerType) {
		name += "OpenXR Controller ";
	}

	if (unStringSectionsToInclude & VRInputString_InputSource) {
		// TODO: what should go here?
		// name += "Something ";
	}

	if (name.size() > 0) {
		// Remove trailing space
		name.erase(name.size() - 1);
	}

	const char* str = name.c_str();

	size_t i;
	for (i = 0; str[i] && i < unNameArraySize - 1; ++i) {
		pchNameArray[i] = str[i];
	}
	pchNameArray[i] = 0;

	return str[i] ? VRInputError_MaxCapacityReached : VRInputError_None;
}

EVRInputError BaseInput::GetOriginTrackedDeviceInfo(VRInputValueHandle_t origin, InputOriginInfo_t* info, uint32_t unOriginInfoSize)
{
	// A null or wrong-sized struct is an argument error, as SteamVR answers (VRInputError_InvalidParam)
	if (!info || unOriginInfoSize != sizeof(InputOriginInfo_t))
		return VRInputError_InvalidParam;
	memset(info, 0, unOriginInfoSize);

	if (origin == vr::k_ulInvalidInputValueHandle)
		return vr::VRInputError_InvalidHandle;

	std::shared_ptr<ITrackedDevice> dev = ivhToDev(origin);

	if (!dev)
		return vr::VRInputError_InvalidHandle;

	info->trackedDeviceIndex = dev->DeviceIndex();
	info->devicePath = origin;
	strcpy_arr(info->rchRenderModelComponentName, "getorigintrackeddeviceinfo_testing"); // TODO figure out how this should work

	return VRInputError_None;
}

/** Retrieves useful information about the bindings for an action */
EVRInputError BaseInput::GetActionBindingInfo(VRActionHandle_t actionHandle, OOVR_InputBindingInfo_t* bindingInfo,
    uint32_t unBindingInfoSize, uint32_t unBindingInfoCount, uint32_t* punReturnedBindingInfoCount)
{
	if (punReturnedBindingInfoCount)
		*punReturnedBindingInfoCount = 0;
	// A null or wrong-sized struct is an argument error, as SteamVR answers (VRInputError_InvalidParam)
	if (unBindingInfoSize != sizeof(OOVR_InputBindingInfo_t) || (unBindingInfoCount && !bindingInfo))
		return VRInputError_InvalidParam;
	if (bindingInfo)
		memset(bindingInfo, 0, (size_t)unBindingInfoSize * unBindingInfoCount);

	// FIXME support any number of sources
	// TODO does this support passing in unBindingInfoSize=0 and reading the required size? Check with SteamVR.
	GET_ACTION_FROM_HANDLE(action, actionHandle);

	XrBoundSourcesForActionEnumerateInfo enumInfo = { XR_TYPE_BOUND_SOURCES_FOR_ACTION_ENUMERATE_INFO };
	enumInfo.action = action->xr;
	uint32_t sourcesCount = 0;
	OC_XR_LOG(xrEnumerateBoundSourcesForAction(xr_session.get(), &enumInfo, 0, &sourcesCount, nullptr));
	std::vector<XrPath> boundActionPaths(sourcesCount);
	OC_XR_LOG(xrEnumerateBoundSourcesForAction(xr_session.get(), &enumInfo, boundActionPaths.size(), &sourcesCount, boundActionPaths.data()));

	// TODO should we return an error if there are no sources bound?

	size_t count = boundActionPaths.size();
	if (count > unBindingInfoCount)
		count = unBindingInfoCount;
	if (punReturnedBindingInfoCount)
		*punReturnedBindingInfoCount = count;

	for (size_t i = 0; i < count; i++) {
		OOVR_InputBindingInfo_t& info = bindingInfo[i];
		XrPath path = boundActionPaths.at(i);

		uint32_t pathLen = 0;
		OC_XR_LOG(xrPathToString(xr_instance, path, 0, &pathLen, nullptr));
		if (pathLen == 0)
			continue; // the path couldn't be read: this binding is left out
		std::vector<char> chars(pathLen);
		OC_XR_LOG(xrPathToString(xr_instance, path, chars.size(), &pathLen, chars.data()));
		std::string pathStr(chars.data(), chars.size());

		std::vector<std::string> parts;
		stringSplit(pathStr, parts);
		if (parts.empty())
			continue;

		// The last part of the string - which is something like '/user/hand/right/input/a/click' - is the mode
		strcpy_arr(info.rchModeName, parts.back().c_str());

		// Hardcode the first three parts of the string as being the device path, and the 4th and 5th ones as
		// being the input path.
		std::string devicePath = pathFromParts({ parts.at(0), parts.at(1), parts.at(2) });
		std::string inputPath = pathFromParts({ parts.at(3), parts.at(4) });
		strcpy_arr(info.rchDevicePathName, devicePath.c_str());
		strcpy_arr(info.rchInputPathName, inputPath.c_str());

		// FIXME replace this initial hacky thing
		switch (action->type) {
		case ActionType::Boolean:
			strcpy_arr(bindingInfo->rchModeName, "button");
			strcpy_arr(bindingInfo->rchInputSourceType, "button");
			break;
		case ActionType::Vector1:
			strcpy_arr(bindingInfo->rchModeName, "trigger");
			strcpy_arr(bindingInfo->rchInputSourceType, "trigger");
			break;
		case ActionType::Vector2:
			strcpy_arr(bindingInfo->rchModeName, "joystick");
			strcpy_arr(bindingInfo->rchInputSourceType, "joystick");
			break;
		default:
			OOVR_LOGF("Unimplemented action type %d for %s: no mode name", static_cast<int>(action->type), pathStr.c_str());
			break;
		}
	}

	return VRInputError_None;
}

EVRInputError BaseInput::ShowActionOrigins(VRActionSetHandle_t actionSetHandle, VRActionHandle_t ulActionHandle)
{
	// Nothing is shown, and the game goes on as if it were
	OC_UNSUPPORTED(VRInputError_None, "ShowActionOrigins: nothing is shown");
}
EVRInputError BaseInput::ShowBindingsForActionSet(VR_ARRAY_COUNT(unSetCount) VRActiveActionSet_t* pSets, uint32_t unSizeOfVRSelectedActionSet_t,
    uint32_t unSetCount, VRInputValueHandle_t originToHighlight)
{
	// Nothing is shown, and the game goes on as if it were
	OC_UNSUPPORTED(VRInputError_None, "ShowBindingsForActionSet: nothing is shown");
}

EVRInputError BaseInput::GetComponentStateForBinding(const char* pchRenderModelName, const char* pchComponentName,
    const OOVR_InputBindingInfo_t* pOriginInfo, uint32_t unBindingInfoSize, uint32_t unBindingInfoCount,
    vr::RenderModel_ComponentState_t* pComponentState)
{
	// No render model component follows a binding yet
	if (pComponentState)
		*pComponentState = {};
	return VRInputError_NoData;
}

bool BaseInput::IsUsingLegacyInput()
{
	return usingLegacyInput;
}

// Interestingly enough this was added to IVRInput_007 without bumping the version number - that's fine since it's
// at the end of the vtable, but it's interesting that the version has always been bumped for this in the past.
EVRInputError BaseInput::OpenBindingUI(const char* pchAppKey, VRActionSetHandle_t ulActionSetHandle,
    VRInputValueHandle_t ulDeviceHandle, bool bShowOnDesktop)
{
	// The binding editor set in opencomposite.ini, if any; SteamVR answers NoSteam without Steam
	std::string editor = oovr_global_configuration.BindingEditor();
	if (editor.empty())
		OC_UNSUPPORTED(VRInputError_NoSteam, "OpenBindingUI: no bindingEditor in opencomposite.ini");

	std::string key = pchAppKey && pchAppKey[0] ? std::string(pchAppKey) : CurrentProcessAppKey();
	ActionSet* set = ulActionSetHandle != vr::k_ulInvalidActionSetHandle ? cast_ASH(ulActionSetHandle) : nullptr;
	InputValueHandle* device = cast_IVH(ulDeviceHandle);
	// Each value one argument of its own, whatever it holds
	std::string args = "--app-key " + QuoteArgument(key) + " --action-set " + QuoteArgument(set ? set->fullName : "")
	    + " --device " + QuoteArgument(device ? device->path : "");
	std::filesystem::path binary = PathFromUtf8(editor);
	if (!LaunchProcess(binary, args, binary.parent_path()))
		OC_UNSUPPORTED(VRInputError_NoSteam, "OpenBindingUI: the binding editor %s didn't start", editor.c_str());
	return VRInputError_None;
}

EVRInputError BaseInput::GetBindingVariant(vr::VRInputValueHandle_t ulDevicePath, char* pchVariantArray, uint32_t unVariantArraySize)
{
	if (!pchVariantArray || unVariantArraySize == 0) {
		return VRInputError_MaxCapacityReached;
	}

	OOVR_SOFT_ABORT("GetBindingVariant not implemented");

	*pchVariantArray = 0;
	return VRInputError_None;
}

BaseInput::Action* BaseInput::cast_AH(VRActionHandle_t handle)
{
	return actions.LookupItem((RegHandle)handle);
}

BaseInput::ActionSet* BaseInput::cast_ASH(VRActionSetHandle_t handle)
{
	return actionSets.LookupItem((RegHandle)handle);
}

BaseInput::InputValueHandle* BaseInput::cast_IVH(VRInputValueHandle_t handle)
{
	// nullptr for k_ulInvalidInputValueHandle and for anything GetInputSourceHandle didn't hand out
	if (handle == vr::k_ulInvalidInputValueHandle)
		return nullptr;
	std::lock_guard<std::mutex> guard(LiveHandlesLock());
	if (!LiveHandles().count((const void*)handle))
		return nullptr;
	return (InputValueHandle*)handle;
}

std::shared_ptr<ITrackedDevice> BaseInput::ivhToDev(VRInputValueHandle_t handle)
{
	const InputValueHandle* ivh = cast_IVH(handle);
	if (!ivh)
		return nullptr;

	ITrackedDevice::TrackedDeviceType hand = ITrackedDevice::HAND_NONE;
	switch (ivh->type) {
	case InputSource::HAND_LEFT:
		hand = ITrackedDevice::HAND_LEFT;
		break;
	case InputSource::HAND_RIGHT:
		hand = ITrackedDevice::HAND_RIGHT;
		break;
	default:
		OOVR_SOFT_ABORTF("Cannot get invalid device from handle '%s'", ivh->path.c_str());
		return nullptr;
	}

	return BackendManager::Instance().GetDeviceByHand(hand);
}

bool BaseInput::checkRestrictToDevice(vr::VRInputValueHandle_t restrictToDevice, XrPath subactionPath)
{
	if (restrictToDevice == vr::k_ulInvalidInputValueHandle)
		return true;

	const InputValueHandle* ivh = cast_IVH(restrictToDevice);
	return ivh && subactionPath == ivh->devicePath;
}

VRInputValueHandle_t BaseInput::activeOriginFromSubaction(Action* action, const char* subactionPath)
{
	// FIXME the docs for xrEnumerateBoundSourcesForAction are wrong and will be updated (source: rpavlik). They don't
	//  have to return a path listed in the input profile, they can be literally anything (not even a /user/hand/<side>
	//  prefix is guaranteed). Thus we'll need some sophisticated lying to the application about this.

	// If it's time for an update and this isn't a virtual input, update its sources
	if (syncSerial >= action->nextSourcesUpdate && action->xr) {
		// Get the attached sources
		XrBoundSourcesForActionEnumerateInfo enumInfo = { XR_TYPE_BOUND_SOURCES_FOR_ACTION_ENUMERATE_INFO };
		enumInfo.action = action->xr;
		// On failure (or a count beyond sources, from SIZE_INSUFFICIENT) the sources are none, or the array's worth
		if (XR_FAILED(xrEnumerateBoundSourcesForAction(xr_session.get(), &enumInfo,
		        std::size(action->sources), &action->sourcesCount, action->sources)))
			action->sourcesCount = 0;
		action->sourcesCount = std::min<uint32_t>(action->sourcesCount,
		    (uint32_t)std::min(std::size(action->sources), std::size(action->sourceNames)));

		// Convert the source paths to strings
		char buff[256] = {};
		for (int i = 0; i < (int)action->sourcesCount; i++) {
			ZeroMemory(buff, sizeof(buff));
			uint32_t length = 0;
			OC_XR_LOG(xrPathToString(xr_instance, action->sources[i], sizeof(buff) - 1, &length, buff));
			action->sourceNames[i] = buff;
		}

		// Wait 200 updates. This obviously depends on the framerate but probably only changes if a device is
		// disconnected or reconnected. It's unlikely it'll be an issue, but just in case randomise the timer
		// to prevent a thundering herd problem.
		action->nextSourcesUpdate = syncSerial + 200 + ((int)std::rand() % 100); // NOLINT(cert-msc50-cpp)
	}

	// Go through the input sources and find the first one that starts with the subaction path
	for (int i = 0; i < (int)action->sourcesCount; i++) {
		const std::string& str = action->sourceNames[i];
		if (strncmp(str.c_str(), subactionPath, strlen(subactionPath)) == 0) {
			VRInputValueHandle_t handle = vr::k_ulInvalidInputValueHandle;
			GetInputSourceHandle(str.c_str(), &handle);
			return handle;
		}
	}

	// Couldn't find one? Just use the subaction path as the device itself. Kinda ugly but it's unlikely to cause
	// an actual crash, and we'll soft-abort on it since this shouldn't actually happen, unless the user has just
	// connected a device and pressed a button before it had time to update.
	OOVR_SOFT_ABORTF("Couldn't find matching source for subaction path '%s' on action '%s'", subactionPath, action->fullName.c_str());
	VRInputValueHandle_t handle = vr::k_ulInvalidInputValueHandle;
	GetInputSourceHandle(subactionPath, &handle);
	return handle;
}

bool BaseInput::GetLegacyControllerState(vr::TrackedDeviceIndex_t controllerDeviceIndex, vr::VRControllerState_t* state)
{
	*state = {};

	// FIXME implement packetNum
	static int i = 0;
	state->unPacketNum = i++; // Not exactly thread safe

	// TODO for performance reasons, is it worth grabbing the results once and reusing them until xrSyncActions is called?

	int hand = DeviceIndexToHandId(controllerDeviceIndex);
	if (hand == -1)
		return false;
	LegacyControllerActions& ctrl = legacyControllers[hand];

	auto bindButton = [state](XrAction action, XrAction touch, int shift) {
		XrActionStateGetInfo getInfo = { XR_TYPE_ACTION_STATE_GET_INFO };
		XrActionStateBoolean xs = { XR_TYPE_ACTION_STATE_BOOLEAN };

		if (action) {
			getInfo.action = action;
			OC_XR_LOG(xrGetActionStateBoolean(xr_session.get(), &getInfo, &xs));
			state->ulButtonPressed |= (uint64_t)(xs.currentState != 0) << shift;
		}

		if (touch != XR_NULL_HANDLE) {
			getInfo.action = touch;
			OC_XR_LOG(xrGetActionStateBoolean(xr_session.get(), &getInfo, &xs));
			state->ulButtonTouched |= (uint64_t)(xs.currentState != 0) << shift;
		}
	};

	// Read the buttons

	bindButton(ctrl.system, XR_NULL_HANDLE, vr::k_EButton_System);
	bindButton(ctrl.btnA, ctrl.btnATouch, vr::k_EButton_A);
	bindButton(ctrl.menu, ctrl.menuTouch, vr::k_EButton_ApplicationMenu);
	bindButton(ctrl.stickBtn, ctrl.stickBtnTouch, vr::k_EButton_SteamVR_Touchpad);
	bindButton(XR_NULL_HANDLE, ctrl.trackpadTouch, vr::k_EButton_SteamVR_Touchpad);
	bindButton(ctrl.gripClick, XR_NULL_HANDLE, vr::k_EButton_Grip);
	bindButton(ctrl.triggerClick, ctrl.triggerTouch, vr::k_EButton_SteamVR_Trigger);
	bindButton(XR_NULL_HANDLE, XR_NULL_HANDLE, vr::k_EButton_Axis2); // FIXME clean up? Is this the grip?

	// Read the analogue values
	auto readFloat = [](XrAction action) -> float {
		if (!action)
			return 0;

		XrActionStateGetInfo getInfo = { XR_TYPE_ACTION_STATE_GET_INFO };
		getInfo.action = action;

		XrActionStateFloat as = { XR_TYPE_ACTION_STATE_FLOAT };
		OC_XR_LOG(xrGetActionStateFloat(xr_session.get(), &getInfo, &as));
		if (as.isActive) {
			return as.currentState;
		} else {
			return 0;
		}
	};

	VRControllerAxis_t& thumbstick = state->rAxis[0];
	thumbstick.x = readFloat(ctrl.stickX);
	thumbstick.y = readFloat(ctrl.stickY);

	VRControllerAxis_t& trigger = state->rAxis[1];
	trigger.x = readFloat(ctrl.trigger);
	trigger.y = 0;

	VRControllerAxis_t& grip = state->rAxis[2];
	grip.x = readFloat(ctrl.grip);
	grip.y = 0;

	// SteamVR seemingly writes to these two axis to represent finger curl on legacy input.
	VRSkeletalSummaryData_t skeletonData{};
	if (xr_gbl && xr_gbl->handTrackingProperties.supportsHandTracking) {
		getRealSkeletalSummary((ITrackedDevice::TrackedDeviceType)hand, &skeletonData);
	} else {
		return true;
	}

	VRControllerAxis_t& fingers = state->rAxis[3];
	fingers.x = skeletonData.flFingerCurl[1] * 1.66f * 1.33f;
	fingers.y = skeletonData.flFingerCurl[2] * 1.66f;

	VRControllerAxis_t& fingers2 = state->rAxis[4];
	fingers2.x = skeletonData.flFingerCurl[3] * 1.66f;
	fingers2.y = skeletonData.flFingerCurl[4] * 1.66f;

	return true;
}

void BaseInput::TriggerLegacyHapticPulse(vr::TrackedDeviceIndex_t controllerDeviceIndex, uint64_t durationNanos)
{
	int hand = DeviceIndexToHandId(controllerDeviceIndex);
	if (hand == -1)
		return;
	LegacyControllerActions& ctrl = legacyControllers[hand];

	if (!ctrl.haptic) {
		OOVR_LOG_ONCE("Cannot trigger haptic pulse, no haptic action present");
		return;
	}

	XrHapticActionInfo info = { XR_TYPE_HAPTIC_ACTION_INFO };
	info.action = ctrl.haptic;

	XrHapticVibration vibration = { XR_TYPE_HAPTIC_VIBRATION };
	vibration.frequency = XR_FREQUENCY_UNSPECIFIED;
	vibration.duration = durationNanos;
	vibration.amplitude = 1;

	OC_XR_LOG(xrApplyHapticFeedback(xr_session.get(), &info, (XrHapticBaseHeader*)&vibration));
}

int BaseInput::DeviceIndexToHandId(vr::TrackedDeviceIndex_t idx)
{
	std::shared_ptr<ITrackedDevice> dev = BackendManager::Instance().GetDevice(idx);
	if (!dev)
		return -1;

	ITrackedDevice::TrackedDeviceType hand = dev->GetHand();

	switch (hand) {
	case ITrackedDevice::HAND_LEFT:
		return 0;
	case ITrackedDevice::HAND_RIGHT:
		return 1;
	default:
		return -1;
	}
}

void BaseInput::GetHandSpace(vr::TrackedDeviceIndex_t index, XrSpace& space, bool aimPose)
{
	space = XR_NULL_HANDLE;

	// If the manifest isn't loaded yet (still on the first frame) return null
	if (!hasLoadedActions)
		return;

	std::shared_ptr<ITrackedDevice> dev = BackendManager::Instance().GetDevice(index);
	if (!dev)
		return;

	ITrackedDevice::TrackedDeviceType hand = dev->GetHand();
	GetHandSpace(hand, space, aimPose);
}

void BaseInput::GetHandSpace(ITrackedDevice::TrackedDeviceType hand, XrSpace& space, bool aimPose)
{
	LegacyControllerActions& ctrl = legacyControllers[hand];

	space = aimPose ? ctrl.aimPoseSpace : ctrl.gripPoseSpace;
}

bool BaseInput::AreActionsLoaded()
{
	return hasLoadedActions;
}

ITrackedDevice::TrackedDeviceType BaseInput::ParseAndRemoveHandPrefix(std::string& toModify)
{
	static std::string leftPrefix = "/user/hand/left/";
	static std::string rightPrefix = "/user/hand/right/";

	if (toModify.starts_with(leftPrefix)) {
		toModify.erase(toModify.begin(), toModify.begin() + (int)leftPrefix.size());
		return ITrackedDevice::HAND_LEFT;
	}

	if (toModify.starts_with(rightPrefix)) {
		toModify.erase(toModify.begin(), toModify.begin() + (int)rightPrefix.size());
		return ITrackedDevice::HAND_RIGHT;
	}

	return ITrackedDevice::HAND_NONE;
}

EVRInputError BaseInput::GetEyeTrackingDataRelativeToNow(VRActionHandle_t action, ETrackingUniverseOrigin eOrigin,
    float fPredictedSecondsFromNow, VREyeTrackingData_t* pEyeTrackingData, uint32_t ulEyeTrackingDataSize)
{
	if (pEyeTrackingData)
		memset(pEyeTrackingData, 0, ulEyeTrackingDataSize);
	return VRInputError_NoData;
}

EVRInputError BaseInput::GetEyeTrackingDataForNextFrame(VRActionHandle_t action, ETrackingUniverseOrigin eOrigin,
    VREyeTrackingData_t* pEyeTrackingData, uint32_t ulEyeTrackingDataSize)
{
	return GetEyeTrackingDataRelativeToNow(action, eOrigin, 0, pEyeTrackingData, ulEyeTrackingDataSize);
}
