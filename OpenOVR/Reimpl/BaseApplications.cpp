#include "stdafx.h"
#include "Misc/EnumNames.h"
#define BASE_IMPL
#include "BaseApplications.h"
#include "BaseClientCore.h"
#include "Misc/AppManifest.h"
#include "Misc/Process.h"
#include "Misc/SharedStore.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <set>
#include <string>

using namespace vr;

// So I can easily use it for return types
typedef OOVR_EVRApplicationError EVRApplicationError;

// The application registry lives in the store's Applications document, shared by every OpenComposite program
// of the user:
//   {"manifests": {"<path>": {"temporary": bool, "owner_pid": n, "apps": [entries]}},
//    "autolaunch": {"<key>": bool}, "mime_defaults": {"<type>": "<key>"}, "pids": {"<pid>": "<key>"},
//    "last_launch": {"<key>": unix_seconds}}
// and the scene application in the Scene document: {"pid": n, "app_key": "...", "since_ms": n}.

namespace {

// Copies text into an app's buffer: cut short and terminated if it doesn't fit. True if it fitted.
bool CopyOut(const std::string& text, char* buffer, uint32_t size)
{
	if (buffer && size) {
		size_t n = std::min<size_t>(text.size(), size - 1);
		memcpy(buffer, text.data(), n);
		buffer[n] = 0;
	}
	return buffer && size >= text.size() + 1;
}

void Clear(char* buffer, uint32_t size)
{
	if (buffer && size)
		buffer[0] = 0;
}

std::string ManifestKey(const char* path)
{
	// The full path as given, normalised, so adding the same file twice is one manifest
	std::filesystem::path p = PathFromUtf8(path);
	std::error_code ec;
	if (p.is_relative())
		p = std::filesystem::absolute(p, ec);
	return PathToUtf8(p.lexically_normal());
}

uint64_t UnixSeconds()
{
	using namespace std::chrono;
	return (uint64_t)duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

uint64_t NowMs()
{
	using namespace std::chrono;
	return (uint64_t)duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// A process id kept as a document key; 0 for anything else
uint32_t ParsePid(const std::string& text)
{
	char* end = nullptr;
	unsigned long v = strtoul(text.c_str(), &end, 10);
	return end && *end == '\0' && v <= UINT32_MAX ? (uint32_t)v : 0;
}

// A process id kept as a document value; 0 for anything else
uint32_t PidOf(const Json::Value& v)
{
	return v.isUInt() ? v.asUInt() : 0;
}

// A document value of the type asked for, else the fallback: jsoncpp throws converting most others
std::string StringOr(const Json::Value& v, const std::string& fallback = std::string())
{
	return v.isString() ? v.asString() : fallback;
}

bool BoolOr(const Json::Value& v, bool fallback)
{
	return v.isBool() ? v.asBool() : fallback;
}

uint64_t UInt64Or(const Json::Value& v, uint64_t fallback)
{
	return v.isUInt64() ? v.asUInt64() : fallback;
}

// Edits the Applications document, first dropping what programs that ended left: their process ids, and the
// temporary manifests they added
bool UpdateApplications(SharedStore& store, const std::function<void(Json::Value& doc)>& edit)
{
	return store.Update(StoreDoc::Applications, [&](Json::Value& doc) {
		if (doc.isMember("pids") && doc["pids"].isObject()) {
			for (const std::string& p : doc["pids"].getMemberNames())
				if (!SharedStore::IsProcessAlive(ParsePid(p)))
					doc["pids"].removeMember(p);
		}
		if (doc.isMember("manifests") && doc["manifests"].isObject()) {
			for (const std::string& path : doc["manifests"].getMemberNames()) {
				const Json::Value& m = doc["manifests"][path];
				if (m.isObject() && m["temporary"].isBool() && m["temporary"].asBool() && !SharedStore::IsProcessAlive(PidOf(m["owner_pid"])))
					doc["manifests"].removeMember(path);
			}
		}
		edit(doc);
	});
}

std::string Join(const std::vector<std::string>& items)
{
	std::string out;
	for (const std::string& s : items) {
		if (!out.empty())
			out += ",";
		out += s;
	}
	return out;
}

// The MIME types an app opens: its manifest's, and those it was made the default for
std::vector<std::string> MimeTypesOf(const AppManifestEntry& app, const Json::Value& doc)
{
	std::vector<std::string> types = app.mimeTypes;
	const Json::Value& defaults = doc["mime_defaults"];
	if (defaults.isObject()) {
		for (const std::string& type : defaults.getMemberNames()) {
			if (StringOr(defaults[type]) == app.appKey && std::find(types.begin(), types.end(), type) == types.end())
				types.push_back(type);
		}
	}
	return types;
}

} // namespace

SharedStore& BaseApplications::Store()
{
	return store ? *store : SharedStore::Get();
}

std::optional<AppManifestEntry> BaseApplications::Find(const char* pchAppKey)
{
	if (!pchAppKey || !pchAppKey[0])
		return std::nullopt;
	for (AppManifestEntry& e : InstalledApplications(Store()))
		if (e.appKey == pchAppKey)
			return std::move(e);
	return std::nullopt;
}

void BaseApplications::SetSceneProcess(bool running)
{
	SetSceneProcess(SharedStore::Get(), running);
}

void BaseApplications::SetSceneProcess(SharedStore& store, bool running)
{
	uint32_t pid = oc_current_pid();
	std::string key = running ? CurrentProcessAppKey(store) : std::string();
	store.Update(StoreDoc::Scene, [&](Json::Value& scene) {
		if (running) {
			scene = Json::Value(Json::objectValue);
			scene["pid"] = pid;
			scene["app_key"] = key;
			scene["since_ms"] = (Json::UInt64)NowMs();
		} else if (scene.isObject() && PidOf(scene["pid"]) == pid) {
			// Only our own: another program may have become the scene application since
			scene = Json::Value(Json::objectValue);
		}
	});
}

// Used by Viveport
EVRApplicationError BaseApplications::AddApplicationManifest(const char* pchApplicationManifestFullPath, bool bTemporary)
{
	if (!pchApplicationManifestFullPath || !pchApplicationManifestFullPath[0])
		return VRApplicationError_InvalidManifest;

	std::string path = ManifestKey(pchApplicationManifestFullPath);
	std::vector<std::string> warnings;
	std::vector<AppManifestEntry> apps = ParseAppManifest(PathFromUtf8(path), warnings);
	for (const std::string& w : warnings)
		OOVR_LOGF("Application manifest: %s", w.c_str());
	if (apps.empty())
		return VRApplicationError_InvalidManifest;

	// A key another manifest already has; replaced if that manifest's file is gone (the game moved, say)
	std::set<std::string> ours, stale;
	for (const AppManifestEntry& e : apps)
		ours.insert(e.appKey);
	for (const AppManifestEntry& e : InstalledApplications(Store())) {
		std::string other = PathToUtf8(e.manifestPath);
		if (!ours.count(e.appKey) || other == path)
			continue;
		std::error_code ec;
		// The same file by another spelling (case, or an 8.3 name, on Windows): this one replaces it
		if (std::filesystem::equivalent(e.manifestPath, PathFromUtf8(path), ec)) {
			stale.insert(other);
			continue;
		}
		if (!std::filesystem::exists(e.manifestPath, ec) && !ec) {
			OOVR_LOGF("Application manifest %s: %s replaces the one from %s, which is gone", path.c_str(), e.appKey.c_str(), other.c_str());
			stale.insert(other);
			continue;
		}
		OOVR_LOGF("Application manifest %s: %s is already installed from %s", path.c_str(), e.appKey.c_str(), other.c_str());
		return VRApplicationError_AppKeyAlreadyExists;
	}

	Json::Value list(Json::arrayValue);
	for (const AppManifestEntry& e : apps)
		list.append(e.ToJson());
	uint32_t pid = oc_current_pid();
	bool written = UpdateApplications(Store(), [&](Json::Value& doc) {
		for (const std::string& gone : stale)
			doc["manifests"].removeMember(gone);
		Json::Value& m = doc["manifests"][path];
		m["temporary"] = bTemporary;
		m["owner_pid"] = pid;
		m["apps"] = list;
	});
	OOVR_LOGF("Added application manifest %s (%d applications, temporary=%d)", path.c_str(), (int)apps.size(), bTemporary);
	// The store not written only loses the registry: answered as before it was kept
	if (!written)
		OOVR_LOG("Application manifest not kept: the store can't be written");
	return VRApplicationError_None;
}

EVRApplicationError BaseApplications::RemoveApplicationManifest(const char* pchApplicationManifestFullPath)
{
	if (!pchApplicationManifestFullPath || !pchApplicationManifestFullPath[0])
		return VRApplicationError_InvalidManifest;
	std::string path = ManifestKey(pchApplicationManifestFullPath);
	bool written = UpdateApplications(Store(), [&](Json::Value& doc) {
		if (doc.isMember("manifests") && doc["manifests"].isObject())
			doc["manifests"].removeMember(path);
	});
	return written ? VRApplicationError_None : VRApplicationError_IPCFailed;
}

bool BaseApplications::IsApplicationInstalled(const char* pchAppKey)
{
	return Find(pchAppKey).has_value();
}
uint32_t BaseApplications::GetApplicationCount()
{
	return (uint32_t)InstalledApplications(Store()).size();
}
EVRApplicationError BaseApplications::GetApplicationKeyByIndex(uint32_t unApplicationIndex, VR_OUT_STRING() char* pchAppKeyBuffer, uint32_t unAppKeyBufferLen)
{
	Clear(pchAppKeyBuffer, unAppKeyBufferLen);
	std::vector<AppManifestEntry> apps = InstalledApplications(Store());
	if (unApplicationIndex >= apps.size())
		return VRApplicationError_InvalidIndex;
	return CopyOut(apps[unApplicationIndex].appKey, pchAppKeyBuffer, unAppKeyBufferLen) ? VRApplicationError_None
	                                                                                     : VRApplicationError_BufferTooSmall;
}
EVRApplicationError BaseApplications::GetApplicationKeyByProcessId(uint32_t unProcessId, VR_OUT_STRING() char* pchAppKeyBuffer, uint32_t unAppKeyBufferLen)
{
	Clear(pchAppKeyBuffer, unAppKeyBufferLen);
	std::string key;
	if (unProcessId == oc_current_pid()) {
		key = CurrentProcessAppKey(Store());
	} else if (SharedStore::IsProcessAlive(unProcessId)) {
		Json::Value doc = Store().Read(StoreDoc::Applications);
		std::string pid = std::to_string(unProcessId);
		if (doc["pids"].isObject() && doc["pids"][pid].isString())
			key = doc["pids"][pid].asString();
		Json::Value scene = Store().Read(StoreDoc::Scene);
		if (key.empty() && PidOf(scene["pid"]) == unProcessId)
			key = StringOr(scene["app_key"]);
	}
	if (key.empty())
		return VRApplicationError_UnknownApplication;
	return CopyOut(key, pchAppKeyBuffer, unAppKeyBufferLen) ? VRApplicationError_None : VRApplicationError_BufferTooSmall;
}

EVRApplicationError BaseApplications::Launch(const AppManifestEntry& app, const std::string& extraArguments)
{
	if (app.isTemplate)
		return VRApplicationError_IsTemplate;

	std::optional<uint32_t> pid;
	if (app.launchType == "url") {
		if (!OpenUrl(app.url))
			return VRApplicationError_LaunchFailed;
	} else {
		std::string arguments = app.arguments;
		if (!extraArguments.empty())
			arguments += (arguments.empty() ? "" : " ") + extraArguments;
		pid = LaunchProcess(app.ResolvedBinary(), arguments, app.ResolvedWorkingDirectory());
		if (!pid)
			return VRApplicationError_LaunchFailed;
	}

	OOVR_LOGF("Launched application %s", app.appKey.c_str());
	uint64_t now = UnixSeconds();
	UpdateApplications(Store(), [&](Json::Value& doc) {
		doc["last_launch"][app.appKey] = (Json::UInt64)now;
		if (pid)
			doc["pids"][std::to_string(*pid)] = app.appKey;
	});
	return VRApplicationError_None;
}

EVRApplicationError BaseApplications::LaunchApplication(const char* pchAppKey)
{
	std::optional<AppManifestEntry> app = Find(pchAppKey);
	if (!app)
		return VRApplicationError_UnknownApplication;
	if (app->isDashboardOverlay)
		return VRApplicationError_InvalidApplication; // LaunchDashboardOverlay's
	return Launch(*app, "");
}
EVRApplicationError BaseApplications::LaunchTemplateApplication(const char* pchTemplateAppKey, const char* pchNewAppKey, VR_ARRAY_COUNT(unKeys) const AppOverrideKeys_t* pKeys, uint32_t unKeys)
{
	std::optional<AppManifestEntry> templ = Find(pchTemplateAppKey);
	if (!templ)
		return VRApplicationError_UnknownApplication;
	if (!templ->isTemplate)
		return VRApplicationError_InvalidApplication;
	if (!pchNewAppKey || !pchNewAppKey[0])
		return VRApplicationError_InvalidParameter;
	if (Find(pchNewAppKey))
		return VRApplicationError_AppKeyAlreadyExists;

	// The template with the overridden keys (named as in a manifest), as a temporary application of this process
	Json::Value app = templ->ToJson();
	app["app_key"] = pchNewAppKey;
	app["is_template"] = false;
	for (uint32_t i = 0; pKeys && i < unKeys; i++) {
		if (!pKeys[i].pchKey || !pKeys[i].pchValue)
			continue;
		std::string key = pKeys[i].pchKey;
		if (key == "binary_path_windows" || key == "binary_path_linux")
			key = "binary_path";
		if (app.isMember(key) && app[key].isString())
			app[key] = pKeys[i].pchValue;
	}
	std::string path = "template:" + std::string(pchNewAppKey);
	uint32_t pid = oc_current_pid();
	UpdateApplications(Store(), [&](Json::Value& doc) {
		Json::Value& m = doc["manifests"][path];
		m["temporary"] = true;
		m["owner_pid"] = pid;
		m["apps"] = Json::Value(Json::arrayValue);
		m["apps"].append(app);
	});
	return Launch(AppManifestEntry::FromJson(app), "");
}
EVRApplicationError BaseApplications::LaunchApplicationFromMimeType(const char* pchMimeType, const char* pchArgs)
{
	char key[256];
	if (!pchMimeType || !GetDefaultApplicationForMimeType(pchMimeType, key, sizeof(key)))
		return VRApplicationError_UnknownApplication;
	std::optional<AppManifestEntry> app = Find(key);
	if (!app)
		return VRApplicationError_UnknownApplication;
	return Launch(*app, pchArgs ? pchArgs : "");
}
EVRApplicationError BaseApplications::LaunchDashboardOverlay(const char* pchAppKey)
{
	std::optional<AppManifestEntry> app = Find(pchAppKey);
	if (!app)
		return VRApplicationError_UnknownApplication;
	if (!app->isDashboardOverlay)
		return VRApplicationError_InvalidApplication;
	// Already running: nothing to do
	if (GetApplicationProcessId(pchAppKey) != 0)
		return VRApplicationError_None;
	return Launch(*app, "");
}
bool BaseApplications::CancelApplicationLaunch(const char* pchAppKey)
{
	// A launch is started at once, so there's never one waiting to be cancelled
	return false;
}
EVRApplicationError BaseApplications::IdentifyApplication(uint32_t unProcessId, const char* pchAppKey)
{
	// Any key, also one without a manifest here: SteamVR knows steam.app.* keys from Steam
	if (!pchAppKey || !pchAppKey[0])
		return VRApplicationError_UnknownApplication;
	uint32_t pid = unProcessId ? unProcessId : oc_current_pid();
	std::string key = pchAppKey;
	// As AddApplicationManifest: not kept if the store can't be written, answered as before it was
	UpdateApplications(Store(), [&](Json::Value& doc) { doc["pids"][std::to_string(pid)] = key; });
	return VRApplicationError_None;
}
uint32_t BaseApplications::GetApplicationProcessId(const char* pchAppKey)
{
	if (!pchAppKey || !pchAppKey[0])
		return 0;
	std::string key = pchAppKey;
	if (CurrentProcessAppKey(Store()) == key)
		return oc_current_pid();
	Json::Value doc = Store().Read(StoreDoc::Applications);
	if (doc["pids"].isObject()) {
		for (const std::string& p : doc["pids"].getMemberNames()) {
			uint32_t pid = ParsePid(p);
			if (StringOr(doc["pids"][p]) == key && SharedStore::IsProcessAlive(pid))
				return pid;
		}
	}
	Json::Value scene = Store().Read(StoreDoc::Scene);
	if (StringOr(scene["app_key"]) == key && SharedStore::IsProcessAlive(PidOf(scene["pid"])))
		return PidOf(scene["pid"]);
	return 0;
}
const char* BaseApplications::GetApplicationsErrorNameFromEnum(EVRApplicationError error)
{
	// As SteamVR answers
	return OC_EnumNameAsSteamVR("EVRApplicationError", (int32_t)error, "Unknown VRApplicationsError (%d)");
}
uint32_t BaseApplications::GetApplicationPropertyString(const char* pchAppKey, EVRApplicationProperty eProperty, VR_OUT_STRING() char* pchPropertyValueBuffer, uint32_t unPropertyValueBufferLen, EVRApplicationError* peError)
{
	Clear(pchPropertyValueBuffer, unPropertyValueBufferLen);
	auto fail = [&](EVRApplicationError e) -> uint32_t {
		if (peError)
			*peError = e;
		return 1; // SteamVR's: an empty string's size
	};
	std::optional<AppManifestEntry> app = Find(pchAppKey);
	if (!app)
		return fail(VRApplicationError_UnknownApplication);

	std::string value;
	switch (eProperty) {
	case VRApplicationProperty_Name_String:
		value = app->nameEnUs.empty() ? app->appKey : app->nameEnUs;
		break;
	case VRApplicationProperty_LaunchType_String:
		value = app->launchType;
		break;
	case VRApplicationProperty_WorkingDirectory_String:
		value = PathToUtf8(app->ResolvedWorkingDirectory());
		break;
	case VRApplicationProperty_BinaryPath_String:
		value = PathToUtf8(app->ResolvedBinary());
		break;
	case VRApplicationProperty_Arguments_String:
		value = app->arguments;
		break;
	case VRApplicationProperty_URL_String:
		value = app->url;
		break;
	case VRApplicationProperty_Description_String:
		value = app->descriptionEnUs;
		break;
	case VRApplicationProperty_NewsURL_String:
		value = app->newsUrl;
		break;
	case VRApplicationProperty_ImagePath_String:
		value = PathToUtf8(app->Resolved(app->imagePath));
		break;
	case VRApplicationProperty_Source_String:
		value = app->source;
		break;
	case VRApplicationProperty_ActionManifestURL_String:
		value = PathToUtf8(app->Resolved(app->actionManifestPath));
		break;
	default:
		return fail(VRApplicationError_UnknownProperty);
	}
	if (value.empty())
		return fail(VRApplicationError_PropertyNotSet);

	bool fits = CopyOut(value, pchPropertyValueBuffer, unPropertyValueBufferLen);
	if (peError)
		*peError = fits ? VRApplicationError_None : VRApplicationError_BufferTooSmall;
	if (!fits)
		Clear(pchPropertyValueBuffer, unPropertyValueBufferLen);
	return (uint32_t)value.size() + 1;
}
bool BaseApplications::GetApplicationPropertyBool(const char* pchAppKey, EVRApplicationProperty eProperty, EVRApplicationError* peError)
{
	auto answer = [&](EVRApplicationError e, bool value) {
		if (peError)
			*peError = e;
		return value;
	};
	std::optional<AppManifestEntry> app = Find(pchAppKey);
	if (!app)
		return answer(VRApplicationError_UnknownApplication, false);
	switch (eProperty) {
	case VRApplicationProperty_IsDashboardOverlay_Bool:
		return answer(VRApplicationError_None, app->isDashboardOverlay);
	case VRApplicationProperty_IsTemplate_Bool:
		return answer(VRApplicationError_None, app->isTemplate);
	case VRApplicationProperty_IsInstanced_Bool:
		return answer(VRApplicationError_None, app->isInstanced);
	case VRApplicationProperty_IsInternal_Bool:
		return answer(VRApplicationError_None, app->isInternal);
	case VRApplicationProperty_WantsCompositorPauseInStandby_Bool:
		return answer(VRApplicationError_None, app->wantsCompositorPauseInStandby);
	default:
		return answer(VRApplicationError_UnknownProperty, false);
	}
}
uint64_t BaseApplications::GetApplicationPropertyUint64(const char* pchAppKey, EVRApplicationProperty eProperty, EVRApplicationError* peError)
{
	auto answer = [&](EVRApplicationError e, uint64_t value) {
		if (peError)
			*peError = e;
		return value;
	};
	std::optional<AppManifestEntry> app = Find(pchAppKey);
	if (!app)
		return answer(VRApplicationError_UnknownApplication, 0);
	if (eProperty != VRApplicationProperty_LastLaunchTime_Uint64)
		return answer(VRApplicationError_UnknownProperty, 0);
	Json::Value doc = Store().Read(StoreDoc::Applications);
	return answer(VRApplicationError_None, doc["last_launch"].isObject() ? UInt64Or(doc["last_launch"][app->appKey], 0) : 0);
}
EVRApplicationError BaseApplications::SetApplicationAutoLaunch(const char* pchAppKey, bool bAutoLaunch)
{
	if (!Find(pchAppKey))
		return VRApplicationError_UnknownApplication;
	std::string key = pchAppKey;
	bool written = UpdateApplications(Store(), [&](Json::Value& doc) { doc["autolaunch"][key] = bAutoLaunch; });
	return written ? VRApplicationError_None : VRApplicationError_IPCFailed;
}
bool BaseApplications::GetApplicationAutoLaunch(const char* pchAppKey)
{
	if (!pchAppKey)
		return false;
	Json::Value doc = Store().Read(StoreDoc::Applications);
	return doc["autolaunch"].isObject() && BoolOr(doc["autolaunch"][pchAppKey], false);
}
EVRApplicationError BaseApplications::SetDefaultApplicationForMimeType(const char* pchAppKey, const char* pchMimeType)
{
	if (!Find(pchAppKey))
		return VRApplicationError_UnknownApplication;
	if (!pchMimeType || !pchMimeType[0])
		return VRApplicationError_InvalidParameter;
	std::string key = pchAppKey, type = pchMimeType;
	bool written = UpdateApplications(Store(), [&](Json::Value& doc) { doc["mime_defaults"][type] = key; });
	return written ? VRApplicationError_None : VRApplicationError_IPCFailed;
}
bool BaseApplications::GetDefaultApplicationForMimeType(const char* pchMimeType, VR_OUT_STRING() char* pchAppKeyBuffer, uint32_t unAppKeyBufferLen)
{
	Clear(pchAppKeyBuffer, unAppKeyBufferLen);
	if (!pchMimeType)
		return false;
	std::string type = pchMimeType;
	Json::Value doc = Store().Read(StoreDoc::Applications);
	std::vector<AppManifestEntry> apps = InstalledApplications(Store());

	// The default set, if it's still installed; else the first application that opens it
	std::string key = doc["mime_defaults"].isObject() ? StringOr(doc["mime_defaults"][type]) : std::string();
	bool installed = false;
	for (const AppManifestEntry& e : apps)
		installed |= e.appKey == key;
	if (!installed) {
		key.clear();
		for (const AppManifestEntry& e : apps) {
			if (std::find(e.mimeTypes.begin(), e.mimeTypes.end(), type) != e.mimeTypes.end()) {
				key = e.appKey;
				break;
			}
		}
	}
	if (key.empty())
		return false;
	return CopyOut(key, pchAppKeyBuffer, unAppKeyBufferLen);
}
bool BaseApplications::GetApplicationSupportedMimeTypes(const char* pchAppKey, VR_OUT_STRING() char* pchMimeTypesBuffer, uint32_t unMimeTypesBuffer)
{
	Clear(pchMimeTypesBuffer, unMimeTypesBuffer);
	std::optional<AppManifestEntry> app = Find(pchAppKey);
	if (!app)
		return false;
	std::vector<std::string> types = MimeTypesOf(*app, Store().Read(StoreDoc::Applications));
	if (types.empty())
		return false;
	return CopyOut(Join(types), pchMimeTypesBuffer, unMimeTypesBuffer);
}
uint32_t BaseApplications::GetApplicationsThatSupportMimeType(const char* pchMimeType, VR_OUT_STRING() char* pchAppKeysThatSupportBuffer, uint32_t unAppKeysThatSupportBuffer)
{
	Clear(pchAppKeysThatSupportBuffer, unAppKeysThatSupportBuffer);
	if (!pchMimeType)
		return 0;
	Json::Value doc = Store().Read(StoreDoc::Applications);
	std::vector<std::string> keys;
	for (const AppManifestEntry& e : InstalledApplications(Store())) {
		std::vector<std::string> types = MimeTypesOf(e, doc);
		if (std::find(types.begin(), types.end(), pchMimeType) != types.end())
			keys.push_back(e.appKey);
	}
	if (keys.empty())
		return 0;
	std::string list = Join(keys);
	CopyOut(list, pchAppKeysThatSupportBuffer, unAppKeysThatSupportBuffer);
	return (uint32_t)list.size() + 1;
}
uint32_t BaseApplications::GetApplicationLaunchArguments(uint32_t unHandle, VR_OUT_STRING() char* pchArgs, uint32_t unArgs)
{
	Clear(pchArgs, unArgs);
	// A MIME type launch starts a new process with the arguments, so no VREvent_ApplicationMimeTypeLoad hands any out
	return 0;
}
EVRApplicationError BaseApplications::GetStartingApplication(VR_OUT_STRING() char* pchAppKeyBuffer, uint32_t unAppKeyBufferLen)
{
	Clear(pchAppKeyBuffer, unAppKeyBufferLen);
	// A launch starts the program at once: nothing is ever waiting to start
	return VRApplicationError_NoApplication;
}
BaseApplications::EVRApplicationTransitionState BaseApplications::GetTransitionState()
{
	return VRApplicationTransition_None;
}
BaseApplications::EVRSceneApplicationState BaseApplications::GetSceneApplicationState()
{
	// A scene application with a session, in this program or another
	return GetCurrentSceneProcessId() != 0 ? EVRSceneApplicationState_Running : EVRSceneApplicationState_None;
}
EVRApplicationError BaseApplications::PerformApplicationPrelaunchCheck(const char* pchAppKey)
{
	if (!Find(pchAppKey))
		return VRApplicationError_UnknownApplication;
	// Nothing is starting, and OpenComposite doesn't stop the scene application for another
	return VRApplicationError_None;
}
const char* BaseApplications::GetApplicationsTransitionStateNameFromEnum(EVRApplicationTransitionState state)
{
	// As SteamVR answers
	return OC_EnumNameAsSteamVR("EVRApplicationTransitionState", (int32_t)state, "Unknown EVRApplicationTransitionState");
}
const char* BaseApplications::GetSceneApplicationStateNameFromEnum(BaseApplications::EVRSceneApplicationState state)
{
	// As SteamVR answers
	return OC_EnumNameAsSteamVR("EVRSceneApplicationState", (int32_t)state, "Unknown EVRSceneApplicationState (%d)");
}
bool BaseApplications::IsQuitUserPromptRequested()
{
	return false;
}
EVRApplicationError BaseApplications::LaunchInternalProcess(const char* pchBinaryPath, const char* pchArguments, const char* pchWorkingDirectory)
{
	if (!pchBinaryPath || !pchBinaryPath[0])
		return VRApplicationError_LaunchFailed;
	std::filesystem::path binary = PathFromUtf8(pchBinaryPath);
	// No working folder: the binary's own
	std::filesystem::path dir = pchWorkingDirectory && pchWorkingDirectory[0] ? PathFromUtf8(pchWorkingDirectory) : binary.parent_path();
	OOVR_LOGF("Launching an internal process: %s %s (in %s)", pchBinaryPath, pchArguments ? pchArguments : "", PathToUtf8(dir).c_str());
	std::optional<uint32_t> pid = LaunchProcess(binary, pchArguments ? pchArguments : "", dir);
	if (!pid)
		return VRApplicationError_LaunchFailed;

	// The new process is part of this application
	std::string key = CurrentProcessAppKey(Store());
	UpdateApplications(Store(), [&](Json::Value& doc) { doc["pids"][std::to_string(*pid)] = key; });
	return VRApplicationError_None;
}
uint32_t BaseApplications::GetCurrentSceneProcessId()
{
	// This program, if it's a scene application (before its session starts, too): the Scene document may still name
	// an earlier one
	if (BaseClientCore::appType == VRApplication_Scene)
		return oc_current_pid();
	Json::Value scene = Store().Read(StoreDoc::Scene);
	uint32_t pid = PidOf(scene["pid"]);
	return pid && SharedStore::IsProcessAlive(pid) ? pid : 0;
}

EVRApplicationError BaseApplications::RegisterSubprocess(uint32_t nPid)
{
	// The subprocess is part of this application
	if (nPid && SharedStore::IsProcessAlive(nPid)) {
		std::string key = CurrentProcessAppKey(Store());
		UpdateApplications(Store(), [&](Json::Value& doc) { doc["pids"][std::to_string(nPid)] = key; });
	}
	return VRApplicationError_None;
}

EVRApplicationError BaseApplications::GetHomeApplication(char* pchAppKeyBuffer, uint32_t unAppKeyBufferLen)
{
	Clear(pchAppKeyBuffer, unAppKeyBufferLen);
	return VRApplicationError_UnknownApplication;
}

EVRApplicationError BaseApplications::SetHomeApplication(const char* pchAppKey)
{
	OC_UNSUPPORTED(VRApplicationError_UnknownApplication, "SetHomeApplication (IVRApplications_001): there's no home "
	                                                      "application on OpenXR");
}
