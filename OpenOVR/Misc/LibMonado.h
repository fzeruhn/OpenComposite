#pragma once

// libmonado: Monado's client library for what OpenXR can't ask, found through the active runtime's manifest
// and loaded at run time. Only when the runtime is Monado and its libmonado's major version is 1; without it every
// answer is OpenComposite's own, as before.

#include "generated/interfaces/vrtypes.h"

#include <chrono>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>

class LibMonado {
public:
	/** The process's: loaded on first use, if the OpenXR runtime is Monado */
	static LibMonado& Get();

	/** Loads libmonado for a runtime of that name (tests); OC_TEST_LIBMONADO_PATH, if set, is the library */
	explicit LibMonado(const std::string& runtimeName);
	~LibMonado();
	LibMonado(const LibMonado&) = delete;
	LibMonado& operator=(const LibMonado&) = delete;

	bool Available() const { return root != nullptr; }

	struct Battery {
		bool present = false, charging = false;
		float charge = 0; // 0 to 1
	};
	/** The battery of the device of that name or serial; nothing without libmonado or such a device */
	std::optional<Battery> BatteryFor(const std::string& deviceName, const std::string& serial);
	/** The battery of the device in a role: "head", "left", "right" */
	std::optional<Battery> BatteryForRole(const char* role);
	/** The name of the device in a role */
	std::optional<std::string> DeviceForRole(const char* role);
	/** Moves a reference space (not used yet: for the room setup's zero poses). False without libmonado. */
	bool SetReferenceSpaceOffset(int referenceSpaceType, const vr::HmdMatrix34_t& offset);

	/** The active OpenXR runtime's manifest, as the loader finds it (XR_RUNTIME_JSON, else the registry or the XDG
	 * config folders) */
	static std::optional<std::filesystem::path> ActiveRuntimeManifest();
	/** The manifest's MND_libmonado_path, resolved against its folder */
	static std::optional<std::filesystem::path> LibraryFromManifest(const std::filesystem::path& manifest);

	struct Api;

private:
	std::optional<uint32_t> DeviceIndex(const std::string& deviceName, const std::string& serial);

	void* library = nullptr;
	void* root = nullptr; // mnd_root_t*
	Api* api = nullptr;
	std::mutex lock;
	// Battery answers kept a second: games ask every frame, and each is a call into the service
	std::map<std::string, std::pair<std::chrono::steady_clock::time_point, std::optional<Battery>>> batteryCache;
};
