#include "stdafx.h"
#define BASE_IMPL
#include "BaseClientCore.h"
#include "steamvr_abi.h"

#include <codecvt>
#include <fstream>
#include <locale>

#include "json/json.h"

#include "Misc/JsonFile.h"

#ifdef _WIN32
#include <ShlObj.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

using namespace vr;

static std::wstring_convert<std::codecvt_utf8<wchar_t>> CHAR_CONV;

// applist.json, apps-config.json and openvrpaths.vrpath are shared with other programs: a broken or half-written
// one reads as empty instead of ending the program, and writes replace the file in one step (Misc/JsonFile.h)
static bool ReadJson(const wstring& path, Json::Value& result)
{
	return oc_read_json_file(std::filesystem::path(path), result);
}

// False if applist.json is there but couldn't be read: writing it back would delete every other program's entry
static bool ReadAppList(const wstring& path, Json::Value& root)
{
	std::error_code ec;
	if (oc_read_json_file(std::filesystem::path(path), root) || (!std::filesystem::exists(path, ec) && !ec))
		return true;
	OOVR_LOG_ONCE("applist.json couldn't be read: leaving it as it is");
	return false;
}

static void WriteJson(const wstring& path, const Json::Value& value)
{
	// The game goes on if it fails: the list only records which programs loaded OpenComposite
	oc_write_json_file(std::filesystem::path(path), value);
}

enum Runtime {
	Default = 0,
	OpenComposite = 1,
	SteamVR = 2,
};

bool BaseClientCore::CheckAppEnabled()
{
#ifndef _WIN32
	OOVR_LOG_ONCE("Launcher configuration not yet supported on Linux");
	return true;
#else
	wstring dlldir = GetDllDir();
	wstring listname = dlldir + L"applist.json";
	wstring configname = dlldir + L"apps-config.json";

	Json::Value tag(Json::ValueType::objectValue);
	// Put information about this app in the tag
	{
		SYSTEMTIME st;
		GetSystemTime(&st);
		FILETIME ft; // Contains a 64-bit value representing the number of 100-nanosecond intervals since January 1, 1601 (UTC).
		SystemTimeToFileTime(&st, &ft);
		ULARGE_INTEGER timestamp;
		timestamp.HighPart = ft.dwHighDateTime;
		timestamp.LowPart = ft.dwLowDateTime;

		// ms is now the number of milliseconds January 1, 1601 (UTC).
		uint64_t ms = timestamp.QuadPart / 10000;

		// Move to the UNIX epoch
		ms -= 11644473600000LL;

		tag["timestamp"] = Json::Value(ms);
	}

	string apppath = GetAppPath();
	Json::Value root;
	if (ReadAppList(listname, root)) {
		root[apppath] = tag;
		WriteJson(listname, root);
	}

	// Check what runtime the app should use, as set in the config menu
	Json::Value config;
	ReadJson(configname, config);
	Json::Value appconf = config[apppath];
	Json::Value global = config["***GlobalConfig"];

	// Keep track of what the selected runtime is
	Runtime selected = Runtime::Default;

	// If a runtime has been set for this program in the GUI, use that
	if (appconf.isMember("runtime")) {
		selected = (Runtime)appconf["runtime"].asInt();
	}

	// If not, then check if a default runtime has been specified
	if (selected == Runtime::Default && global.isMember("default_runtime")) {
		selected = (Runtime)global["default_runtime"].asInt();
	}

	// Finally, default to using OpenComposite
	if (selected == Runtime::Default) {
		selected = Runtime::OpenComposite;
	}

	return selected == Runtime::OpenComposite;
#endif
}

template <typename str_t>
static void TrimPath(str_t& path)
{
	if (path.back() == '\\' || path.back() == '/') {
		path.erase(path.end() - 1);
	}
}

template <typename str_t>
bool EndsWith(const str_t& a, const str_t& b)
{
	if (b.size() > a.size())
		return false;
	return std::equal(a.begin() + a.size() - b.size(), a.end(), b.begin());
}

// Derived from OpenVR
static wstring GetAppSettingsPath()
{
	wstring path;
#if defined(WIN32)
	WCHAR rwchPath[MAX_PATH];

	if (!SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, NULL, 0, rwchPath))) {
		// The paths file then can't be found, which its readers handle
		OOVR_LOG("Failed to find the appdata directory");
		return {};
	}

	path = rwchPath;
#elif defined(OSX)
#error "Unsupported platform"
#elif defined(__linux__)
	// As defined by XDG Base Directory Specification
	// https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html

	const char* pchHome = getenv("XDG_CONFIG_HOME");
	if ((pchHome != NULL) && (pchHome[0] != '\0')) {
		return CHAR_CONV.from_bytes(pchHome);
	}

	//
	// XDG_CONFIG_HOME is not defined, use ~/.config instead
	//
	pchHome = getenv("HOME");
	if (pchHome == NULL) {
		return L"";
	}

	wstring homeDir = CHAR_CONV.from_bytes(pchHome);
	TrimPath(homeDir);
	path = homeDir + L".config";
#else
#error "Unsupported platform"
#endif

	TrimPath(path);

	return path;
}

static wstring GetOpenVRConfigPath()
{
	wstring sConfigPath = GetAppSettingsPath();

#if defined(_WIN32) || defined(__linux__)
	sConfigPath += L"/openvr";
#elif defined(OSX)
	sConfigPath += L"/.openvr";
#else
#error "Unsupported platform"
#endif
	return sConfigPath;
}

string BaseClientCore::GetAlternativeRuntimePath()
{
	wstring regPath = GetOpenVRConfigPath();

#if defined(_WIN32) || defined(__unix__)
	regPath += L"/openvrpaths.vrpath";
#else
#error "Unsupported platform"
#endif

	// "" (logged) if there's no SteamVR to chain to: PlatformGetAlternativeCoreFactory then gives up
	Json::Value root;
	if (!ReadJson(regPath, root)) {
		OOVR_LOG("Cannot start SteamVR: openvrpaths.vrpath couldn't be read");
		return {};
	}

	const Json::Value runtimes = root["runtime"];
	if (!runtimes.isArray()) {
		OOVR_LOG("Cannot start SteamVR: malformed runtime path");
		return {};
	}

	for (unsigned int i = 0; i < runtimes.size(); i++) {
		string path = runtimes[i].asString();
		TrimPath(path);

		if (EndsWith(path, string("\\SteamVR")))
			return path;
	}

	OOVR_LOG("Could not find a usable SteamVR installation");
	return {};
}

EVRInitError BaseClientCore::Init(vr::EVRApplicationType eApplicationType, const char* pStartupInfo)
{
	EVRInitError err;
	VR_InitInternal2(&err, eApplicationType, pStartupInfo);
	return err;
}

void BaseClientCore::Cleanup()
{
	// Note that this object is not affected by the shutdown, as it is handled seperately
	//  from all the other interface objects and is only destroyed when the DLL is unloaded.
	VR_ShutdownInternal();
}

EVRInitError BaseClientCore::IsInterfaceVersionValid(const char* pchInterfaceVersion)
{
	return VR_IsInterfaceVersionValid(pchInterfaceVersion) ? VRInitError_None : VRInitError_Init_InvalidInterface;
}

void* BaseClientCore::GetGenericInterface(const char* pchNameAndVersion, EVRInitError* peError)
{
	return VR_GetGenericInterface(pchNameAndVersion, peError);
}

bool BaseClientCore::BIsHmdPresent()
{
	return VR_IsHmdPresent();
}

const char* BaseClientCore::GetEnglishStringForHmdError(vr::EVRInitError eError)
{
	return VR_GetVRInitErrorAsEnglishDescription(eError);
}

const char* BaseClientCore::GetIDForVRInitError(vr::EVRInitError eError)
{
	return VR_GetVRInitErrorAsSymbol(eError);
}

std::string BaseClientCore::GetAppPath()
{
#ifndef _WIN32
	char appExe[4096];
	ssize_t len = readlink("/proc/self/exe", appExe, sizeof(appExe) - 1);
	return len > 0 ? string(appExe, (size_t)len) : string();
#else
	char appExe[MAX_PATH];
	DWORD len = GetModuleFileNameA(nullptr, appExe, sizeof(appExe));
	return string(appExe, len);
#endif
}

std::wstring BaseClientCore::GetDllDir()
{
#ifndef _WIN32
	// The folder of the shared object this function is in
	Dl_info info{};
	if (!dladdr((void*)&BaseClientCore::GetDllDir, &info) || !info.dli_fname)
		return {};
	string path = info.dli_fname;
	path = path.substr(0, path.find_last_of('/') + 1);
	return CHAR_CONV.from_bytes(path);
#else
	wchar_t buffer[MAX_PATH];
	DWORD len = GetModuleFileNameW(openovr_module_id, buffer, MAX_PATH);
	wstring dllpath(buffer, len);
	return dllpath.substr(0, dllpath.find_last_of('\\') + 1);
#endif
}

void BaseClientCore::SetManifestPath(string filename)
{
	wstring listname = GetDllDir() + L"applist.json";
	Json::Value root;
	if (!ReadAppList(listname, root))
		return;

	Json::Value& tag = root[GetAppPath()];

	// Don't need to update it
	if (tag["manifest"] == filename)
		return;

	tag["manifest"] = filename;
	WriteJson(listname, root);
}
