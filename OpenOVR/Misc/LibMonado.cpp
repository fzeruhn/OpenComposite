#include "stdafx.h"

#include "LibMonado.h"

#include "Misc/AppManifest.h"
#include "Misc/JsonFile.h"

#include <cstdlib>
#include <cmath>
#include <cstring>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace fs = std::filesystem;

// libmonado's calls, as its monado.h declares them (major version 1); every one is looked up by name, and one that
// isn't there leaves that answer OpenComposite's own
extern "C" {
typedef void (*mnd_api_get_version_t)(uint32_t* major, uint32_t* minor, uint32_t* patch); // always succeeds
typedef int (*mnd_root_create_t)(void** out_root);
typedef void (*mnd_root_destroy_t)(void** root);
typedef int (*mnd_root_get_device_count_t)(void* root, uint32_t* out_count);
typedef int (*mnd_root_get_device_info_string_t)(void* root, uint32_t device_index, int prop, const char** out);
typedef int (*mnd_root_get_device_from_role_t)(void* root, const char* role, int32_t* out_index);
typedef int (*mnd_root_get_device_battery_status_t)(void* root, uint32_t device_index, bool* present, bool* charging, float* charge);
struct OcMndPose {
	float orientation[4]; // x, y, z, w
	float position[3];
};
typedef int (*mnd_root_set_reference_space_offset_t)(void* root, int type, const OcMndPose* offset);
}

// libmonado's device properties (mnd_property_t)
constexpr int kMndPropertyNameString = 0;
constexpr int kMndPropertySerialString = 1;

struct LibMonado::Api {
	mnd_api_get_version_t getVersion = nullptr;
	mnd_root_create_t rootCreate = nullptr;
	mnd_root_destroy_t rootDestroy = nullptr;
	mnd_root_get_device_count_t getDeviceCount = nullptr;
	mnd_root_get_device_info_string_t getDeviceInfoString = nullptr;
	mnd_root_get_device_from_role_t getDeviceFromRole = nullptr;
	mnd_root_get_device_battery_status_t getBatteryStatus = nullptr;
	mnd_root_set_reference_space_offset_t setReferenceSpaceOffset = nullptr;
};

namespace {

void* Symbol(void* library, const char* name)
{
#ifdef _WIN32
	return (void*)GetProcAddress((HMODULE)library, name);
#else
	return dlsym(library, name);
#endif
}

void CloseLibrary(void* library)
{
	if (!library)
		return;
#ifdef _WIN32
	FreeLibrary((HMODULE)library);
#else
	dlclose(library);
#endif
}

std::string RuntimeNameOfInstance()
{
	if (xr_instance == XR_NULL_HANDLE)
		return {};
	XrInstanceProperties properties{ XR_TYPE_INSTANCE_PROPERTIES };
	if (XR_FAILED(xrGetInstanceProperties(xr_instance, &properties)))
		return {};
	return properties.runtimeName;
}

} // namespace

LibMonado& LibMonado::Get()
{
	// Never destroyed: at process exit libmonado may already be unloaded (Windows detaches DLLs in its own order) and
	// the game's threads may still be calling it; the OS ends the connection
	static LibMonado* instance = new LibMonado(RuntimeNameOfInstance());
	return *instance;
}

// An environment variable as a path: wide on Windows (the narrow one is in the ANSI code page, not UTF-8)
static std::optional<fs::path> PathFromEnvironment(const char* name, const wchar_t* wideName)
{
#ifdef _WIN32
	(void)name;
	const wchar_t* value = _wgetenv(wideName);
	if (!value || !value[0])
		return std::nullopt;
	return fs::path(value);
#else
	(void)wideName;
	const char* value = getenv(name);
	if (!value || !value[0])
		return std::nullopt;
	return fs::path(value);
#endif
}

std::optional<fs::path> LibMonado::RuntimeManifestFromEnvironment()
{
	return PathFromEnvironment("XR_RUNTIME_JSON", L"XR_RUNTIME_JSON");
}

std::optional<fs::path> LibMonado::ActiveRuntimeManifest()
{
	// XR_RUNTIME_JSON first, as the loader reads it (on Windows, an elevated process's loader ignores it, and so do we)
	bool elevated = false;
#ifdef _WIN32
	HANDLE token = nullptr;
	if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
		TOKEN_ELEVATION elevation{};
		DWORD size = 0;
		if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size))
			elevated = elevation.TokenIsElevated != 0;
		CloseHandle(token);
	}
#endif
	if (std::optional<fs::path> env = RuntimeManifestFromEnvironment(); env && !elevated)
		return env;
#ifdef _WIN32
	wchar_t value[1024];
	DWORD size = sizeof(value);
	if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Khronos\\OpenXR\\1", L"ActiveRuntime", RRF_RT_REG_SZ, nullptr, value, &size) == ERROR_SUCCESS)
		return fs::path(value);
	return std::nullopt;
#else
	std::vector<fs::path> candidates;
	if (const char* xdg = getenv("XDG_CONFIG_HOME"); xdg && xdg[0])
		candidates.push_back(fs::path(xdg) / "openxr" / "1" / "active_runtime.json");
	else if (const char* home = getenv("HOME"); home && home[0])
		candidates.push_back(fs::path(home) / ".config" / "openxr" / "1" / "active_runtime.json");
	candidates.push_back("/etc/xdg/openxr/1/active_runtime.json");
	candidates.push_back("/etc/openxr/1/active_runtime.json");
	for (const fs::path& p : candidates) {
		std::error_code ec;
		if (fs::exists(p, ec))
			return p;
	}
	return std::nullopt;
#endif
}

std::optional<fs::path> LibMonado::LibraryFromManifest(const fs::path& manifest)
{
	Json::Value root;
	std::string error;
	if (!ReadJsonFileLenient(manifest, root, error) || !root.isObject())
		return std::nullopt;
	// Inside "runtime", as Monado writes it; at the top too, in case
	const Json::Value* value = nullptr;
	if (root["runtime"].isObject() && root["runtime"]["MND_libmonado_path"].isString())
		value = &root["runtime"]["MND_libmonado_path"];
	else if (root["MND_libmonado_path"].isString())
		value = &root["MND_libmonado_path"];
	if (!value || value->asString().empty())
		return std::nullopt;
	fs::path p = PathFromUtf8(value->asString());
	// A bare file name (the soname distributions install) is for the loader's search, as the OpenXR loader treats
	// library_path
	if (!p.has_parent_path())
		return p;
	// The manifest's own folder, through every symbolic link on the way (as the loader does)
	std::error_code ec;
	fs::path folder = fs::weakly_canonical(manifest, ec).parent_path();
	if (ec)
		folder = fs::absolute(manifest, ec).parent_path();
	if (p.is_relative())
		p = folder / p;
	return p.lexically_normal();
}

LibMonado::LibMonado(const std::string& runtimeName)
{
	if (runtimeName.rfind("Monado", 0) != 0)
		return; // only Monado has a libmonado
	std::optional<fs::path> path;
	if (std::optional<fs::path> test = PathFromEnvironment("OC_TEST_LIBMONADO_PATH", L"OC_TEST_LIBMONADO_PATH")) {
		path = test;
	} else {
		if (std::optional<fs::path> manifest = ActiveRuntimeManifest())
			path = LibraryFromManifest(*manifest);
	}
	if (!path) {
		OOVR_LOG("libmonado: the runtime's manifest names none; battery levels and roles come from OpenXR alone");
		return;
	}

#ifdef _WIN32
	// A full path: its own folder first for the DLLs it needs (monado.dll's pthreadVCE3.dll and cjson.dll), which
	// LoadLibraryW alone doesn't search. A bare name: Windows' search.
	if (path->has_parent_path())
		library = (void*)LoadLibraryExW(path->c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
	else
		library = (void*)LoadLibraryW(path->c_str());
#else
	library = dlopen(path->c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
	if (!library) {
		OOVR_LOGF("libmonado: couldn't load %s", PathToUtf8(*path).c_str());
		return;
	}

	api = new Api();
	api->getVersion = (mnd_api_get_version_t)Symbol(library, "mnd_api_get_version");
	api->rootCreate = (mnd_root_create_t)Symbol(library, "mnd_root_create");
	api->rootDestroy = (mnd_root_destroy_t)Symbol(library, "mnd_root_destroy");
	api->getDeviceCount = (mnd_root_get_device_count_t)Symbol(library, "mnd_root_get_device_count");
	api->getDeviceInfoString = (mnd_root_get_device_info_string_t)Symbol(library, "mnd_root_get_device_info_string");
	api->getDeviceFromRole = (mnd_root_get_device_from_role_t)Symbol(library, "mnd_root_get_device_from_role");
	api->getBatteryStatus = (mnd_root_get_device_battery_status_t)Symbol(library, "mnd_root_get_device_battery_status");
	api->setReferenceSpaceOffset = (mnd_root_set_reference_space_offset_t)Symbol(library, "mnd_root_set_reference_space_offset");

	uint32_t major = 0, minor = 0, patch = 0;
	if (api->getVersion)
		api->getVersion(&major, &minor, &patch);
	if (major != 1) {
		OOVR_LOGF("libmonado: version %u.%u.%u isn't 1.x; not used", major, minor, patch);
		delete api;
		api = nullptr;
		CloseLibrary(library);
		library = nullptr;
		return;
	}
	if (!api->rootCreate || api->rootCreate(&root) != 0) {
		OOVR_LOG("libmonado: couldn't connect to the Monado service; not used");
		root = nullptr;
		return;
	}
	OOVR_LOGF("libmonado %u.%u.%u: %s", major, minor, patch, PathToUtf8(*path).c_str());
}

LibMonado::~LibMonado()
{
	if (root && api && api->rootDestroy)
		api->rootDestroy(&root);
	delete api;
	// The library stays loaded (only tests destroy an instance: the process's, Get's, is never destroyed)
}

std::optional<uint32_t> LibMonado::DeviceIndex(const std::string& deviceName, const std::string& serial)
{
	if (!api->getDeviceCount || !api->getDeviceInfoString)
		return std::nullopt;
	uint32_t count = 0;
	if (api->getDeviceCount(root, &count) != 0)
		return std::nullopt;
	for (uint32_t i = 0; i < count; i++) {
		const char* name = nullptr;
		const char* devSerial = nullptr;
		if (api->getDeviceInfoString(root, i, kMndPropertyNameString, &name) == 0 && name && !deviceName.empty() && deviceName == name)
			return i;
		if (api->getDeviceInfoString(root, i, kMndPropertySerialString, &devSerial) == 0 && devSerial && !serial.empty() && serial == devSerial)
			return i;
	}
	return std::nullopt;
}

std::optional<LibMonado::Battery> LibMonado::BatteryFor(const std::string& deviceName, const std::string& serial)
{
	if (!Available() || !api->getBatteryStatus)
		return std::nullopt;
	std::lock_guard guard(lock);
	std::optional<uint32_t> index = DeviceIndex(deviceName, serial);
	if (!index)
		return std::nullopt;
	return BatteryAt(*index);
}

LibMonado::Battery LibMonado::BatteryAt(uint32_t index)
{
	Battery b;
	if (!api->getBatteryStatus || api->getBatteryStatus(root, index, &b.present, &b.charging, &b.charge) != 0)
		return Battery{}; // Monado fails it for a device without battery status (MND_ERROR_OPERATION_FAILED)
	return b;
}

std::optional<std::string> LibMonado::DeviceForRole(const char* role)
{
	if (!Available() || !api->getDeviceFromRole || !api->getDeviceInfoString || !role)
		return std::nullopt;
	std::lock_guard guard(lock);
	int32_t index = -1;
	if (api->getDeviceFromRole(root, role, &index) != 0 || index < 0)
		return std::nullopt;
	const char* name = nullptr;
	if (api->getDeviceInfoString(root, (uint32_t)index, kMndPropertyNameString, &name) != 0 || !name)
		return std::nullopt;
	return std::string(name);
}

std::optional<LibMonado::Battery> LibMonado::BatteryForRole(const char* role)
{
	if (!Available() || !api->getBatteryStatus || !role)
		return std::nullopt;
	{
		std::lock_guard guard(lock);
		auto it = batteryCache.find(role);
		if (it != batteryCache.end() && std::chrono::steady_clock::now() - it->second.first < std::chrono::seconds(1))
			return it->second.second;
	}
	// The role's own device (libmonado's roles are device indices): two controllers may share a name
	std::optional<Battery> battery;
	std::lock_guard guard(lock);
	int32_t index = -1;
	if (api->getDeviceFromRole && api->getDeviceFromRole(root, role, &index) == 0 && index >= 0)
		battery = BatteryAt((uint32_t)index);
	batteryCache[role] = { std::chrono::steady_clock::now(), battery };
	return battery;
}

bool LibMonado::SetReferenceSpaceOffset(int referenceSpaceType, const vr::HmdMatrix34_t& m)
{
	if (!Available() || !api->setReferenceSpaceOffset)
		return false;
	// The matrix's rotation as a quaternion
	OcMndPose pose{};
	float trace = m.m[0][0] + m.m[1][1] + m.m[2][2];
	float x, y, z, w;
	if (trace > 0) {
		float s = 0.5f / std::sqrt(trace + 1.0f);
		w = 0.25f / s;
		x = (m.m[2][1] - m.m[1][2]) * s;
		y = (m.m[0][2] - m.m[2][0]) * s;
		z = (m.m[1][0] - m.m[0][1]) * s;
	} else if (m.m[0][0] > m.m[1][1] && m.m[0][0] > m.m[2][2]) {
		float s = 2.0f * std::sqrt(1.0f + m.m[0][0] - m.m[1][1] - m.m[2][2]);
		w = (m.m[2][1] - m.m[1][2]) / s;
		x = 0.25f * s;
		y = (m.m[0][1] + m.m[1][0]) / s;
		z = (m.m[0][2] + m.m[2][0]) / s;
	} else if (m.m[1][1] > m.m[2][2]) {
		float s = 2.0f * std::sqrt(1.0f + m.m[1][1] - m.m[0][0] - m.m[2][2]);
		w = (m.m[0][2] - m.m[2][0]) / s;
		x = (m.m[0][1] + m.m[1][0]) / s;
		y = 0.25f * s;
		z = (m.m[1][2] + m.m[2][1]) / s;
	} else {
		float s = 2.0f * std::sqrt(1.0f + m.m[2][2] - m.m[0][0] - m.m[1][1]);
		w = (m.m[1][0] - m.m[0][1]) / s;
		x = (m.m[0][2] + m.m[2][0]) / s;
		y = (m.m[1][2] + m.m[2][1]) / s;
		z = 0.25f * s;
	}
	pose.orientation[0] = x;
	pose.orientation[1] = y;
	pose.orientation[2] = z;
	pose.orientation[3] = w;
	pose.position[0] = m.m[0][3];
	pose.position[1] = m.m[1][3];
	pose.position[2] = m.m[2][3];
	std::lock_guard guard(lock);
	return api->setReferenceSpaceOffset(root, referenceSpaceType, &pose) == 0;
}
