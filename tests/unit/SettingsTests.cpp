// doctest first: stdafx.h brings in Xlib on Linux, whose macros (None, Bool, ...) clash with doctest's names
#include "doctest.h"

#include "stdafx.h"

#include "Misc/SharedStore.h"
#include "Misc/SteamVRSettings.h"
#include "Reimpl/BaseSettings.h"
#include "../DrvOpenXR/DeviceIdentity.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {
// A fresh folder under the system's temporary one, removed again at the end
struct TempDir {
	fs::path path;
	TempDir()
	{
		auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		path = fs::temp_directory_path() / ("oc_settings_" + std::to_string(stamp));
		fs::create_directories(path);
	}
	~TempDir()
	{
		std::error_code ec;
		fs::remove_all(path, ec);
	}
};

// SteamVR's files for one test: the fixture as the user's file (or none), and no defaults file unless given. Put
// back to finding them at the end, so a developer's own SteamVR never changes a test's answer.
struct SteamVRFiles {
	explicit SteamVRFiles(bool withFixture, std::optional<fs::path> defaults = std::nullopt)
	{
		fs::path none = fs::temp_directory_path() / "oc_settings_no_such_file.vrsettings";
		SteamVRSettingsUseFiles(withFixture ? fs::path(OC_TEST_DATA_DIR) / "steamvr.vrsettings" : none, defaults.value_or(none));
	}
	~SteamVRFiles() { SteamVRSettingsUseFiles(); }
};

// The SteamAppId environment variable for one test
struct AppId {
	explicit AppId(const char* id) { Set(id); }
	~AppId() { Set(nullptr); }
	static void Set(const char* id)
	{
#ifdef _WIN32
		_putenv_s("SteamAppId", id ? id : "");
#else
		if (id)
			setenv("SteamAppId", id, 1);
		else
			unsetenv("SteamAppId");
#endif
	}
};

std::string GetString(BaseSettings& settings, const char* section, const char* key, BaseSettings::EVRSettingsError* error)
{
	char buff[256] = "unchanged";
	settings.GetString(section, key, buff, sizeof(buff), error);
	return buff;
}
} // namespace

TEST_CASE("Settings_set_get_each_type")
{
	TempDir dir;
	SteamVRFiles files(false);
	SharedStore store(dir.path);
	BaseSettings settings(store);
	BaseSettings::EVRSettingsError error = VRSettingsError_IPCFailed;

	settings.SetBool("oc_test", "flag", true, &error);
	CHECK(error == VRSettingsError_None);
	settings.SetInt32("oc_test", "count", -7, &error);
	CHECK(error == VRSettingsError_None);
	settings.SetFloat("oc_test", "scale", 1.25f, &error);
	CHECK(error == VRSettingsError_None);
	settings.SetString("oc_test", "label", "hello", &error);
	CHECK(error == VRSettingsError_None);

	CHECK(settings.GetBool("oc_test", "flag", &error));
	CHECK(error == VRSettingsError_None);
	CHECK(settings.GetInt32("oc_test", "count", &error) == -7);
	CHECK(settings.GetFloat("oc_test", "scale", &error) == 1.25f);
	CHECK(GetString(settings, "oc_test", "label", &error) == "hello");
	CHECK(error == VRSettingsError_None);

	// Stored with the call's JSON type
	Json::Value data = store.Read(StoreDoc::Settings);
	CHECK(data["oc_test"]["flag"].isBool());
	CHECK(data["oc_test"]["count"].isInt());
	CHECK(data["oc_test"]["scale"].isDouble());
	CHECK(data["oc_test"]["label"].isString());

	// Read as other types, as SteamVR converts them
	CHECK(settings.GetFloat("oc_test", "count", &error) == -7.0f);
	CHECK(settings.GetInt32("oc_test", "flag", &error) == 1);
	CHECK(GetString(settings, "oc_test", "scale", &error) == "1.250000");
	settings.SetFloat("oc_test", "whole", 2.0f, &error);
	CHECK(GetString(settings, "oc_test", "whole", &error) == "2.000000");
	CHECK(GetString(settings, "oc_test", "flag", &error) == "true");
	settings.SetString("oc_test", "number", "12.5", &error);
	CHECK(settings.GetFloat("oc_test", "number", &error) == 12.5f);
	CHECK(error == VRSettingsError_None);
	CHECK(settings.GetInt32("oc_test", "label", &error) == 0);
	CHECK(error == VRSettingsError_ReadFailed);

	// A buffer too small: cut short and terminated, with an error
	char shortBuffer[3];
	settings.GetString("oc_test", "label", shortBuffer, sizeof(shortBuffer), &error);
	CHECK(std::string(shortBuffer) == "he");
	CHECK(error == VRSettingsError_ReadFailed);

	// Null section or key: nothing written, no crash
	settings.SetInt32(nullptr, "count", 1, &error);
	CHECK(error == VRSettingsError_WriteFailed);
	settings.SetString("oc_test", "label", nullptr, &error);
	CHECK(error == VRSettingsError_WriteFailed);
	CHECK(settings.GetInt32("oc_test", nullptr, &error) == 0);
	CHECK(error == VRSettingsError_ReadFailed);
}

TEST_CASE("Settings_fall_back_to_steamvr_file")
{
	TempDir dir;
	fs::path defaults = dir.path / "default.vrsettings";
	std::ofstream(defaults) << "{ \"oc_test\": { \"answer\": 1, \"onlyDefault\": \"from defaults\" } }";
	SteamVRFiles files(true, defaults);
	SharedStore store(dir.path / "store");
	BaseSettings settings(store);
	BaseSettings::EVRSettingsError error = VRSettingsError_IPCFailed;

	// The user's file beats SteamVR's defaults, which answer what the user's file doesn't have
	CHECK(settings.GetInt32("oc_test", "answer", &error) == 42);
	CHECK(error == VRSettingsError_None);
	CHECK(GetString(settings, "oc_test", "name", &error) == "forty-two");
	CHECK(settings.GetFloat("oc_test", "ratio", &error) == 0.25f);
	CHECK(GetString(settings, "oc_test", "onlyDefault", &error) == "from defaults");
	CHECK_FALSE(settings.GetBool("steamvr", "enableHomeApp", &error));
	CHECK(error == VRSettingsError_None);
	CHECK(settings.GetFloat("oc_test", "words", &error) == 0.0f);
	CHECK(error == VRSettingsError_ReadFailed);

	// The store beats both
	settings.SetInt32("oc_test", "answer", 7, &error);
	CHECK(settings.GetInt32("oc_test", "answer", &error) == 7);

	// Read again when the file changes
	std::ofstream(defaults) << "{ \"oc_test\": { \"onlyDefault\": \"changed\" } }";
	fs::last_write_time(defaults, fs::last_write_time(defaults) + std::chrono::seconds(5));
	SteamVRSettingsUseFiles(fs::path(OC_TEST_DATA_DIR) / "steamvr.vrsettings", defaults);
	CHECK(GetString(settings, "oc_test", "onlyDefault", &error) == "changed");
}

TEST_CASE("Settings_unset_key_error")
{
	TempDir dir;
	SteamVRFiles files(false);
	SharedStore store(dir.path);
	BaseSettings settings(store);
	BaseSettings::EVRSettingsError error = VRSettingsError_None;

	CHECK_FALSE(settings.GetBool("oc_test", "nobody_set_this", &error));
	CHECK(error == VRSettingsError_UnsetSettingHasNoDefault);
	error = VRSettingsError_None;
	CHECK(settings.GetInt32("oc_test", "nobody_set_this", &error) == 0);
	CHECK(error == VRSettingsError_UnsetSettingHasNoDefault);
	error = VRSettingsError_None;
	CHECK(settings.GetFloat("oc_test", "nobody_set_this", &error) == 0.0f);
	CHECK(error == VRSettingsError_UnsetSettingHasNoDefault);
	error = VRSettingsError_None;
	CHECK(GetString(settings, "oc_test", "nobody_set_this", &error).empty());
	CHECK(error == VRSettingsError_UnsetSettingHasNoDefault);

	// The answers OpenComposite always gave are still there
	CHECK(settings.GetBool("steamvr", "allowReprojection", &error));
	CHECK(error == VRSettingsError_None);
	CHECK(settings.GetFloat("steam.app.123", "resolutionScale", &error) == 100.0f);
	CHECK_FALSE(settings.GetBool("steamvr", "motionSmoothing", &error));
	CHECK(error == VRSettingsError_None);
}

TEST_CASE("Settings_remove_key")
{
	TempDir dir;
	SteamVRFiles files(false);
	SharedStore store(dir.path);
	BaseSettings settings(store);
	BaseSettings::EVRSettingsError error = VRSettingsError_IPCFailed;

	settings.SetInt32("oc_test", "a", 1);
	settings.SetInt32("oc_test", "b", 2);
	settings.SetInt32("oc_other", "c", 3);

	settings.RemoveKeyInSection("oc_test", "a", &error);
	CHECK(error == VRSettingsError_None);
	settings.GetInt32("oc_test", "a", &error);
	CHECK(error == VRSettingsError_UnsetSettingHasNoDefault);
	CHECK(settings.GetInt32("oc_test", "b", &error) == 2);

	settings.RemoveSection("oc_test", &error);
	CHECK(error == VRSettingsError_None);
	settings.GetInt32("oc_test", "b", &error);
	CHECK(error == VRSettingsError_UnsetSettingHasNoDefault);
	CHECK(settings.GetInt32("oc_other", "c", &error) == 3);

	// Removing what isn't there, or a key of a section that isn't, is fine and adds nothing
	settings.RemoveKeyInSection("oc_none", "x", &error);
	CHECK(error == VRSettingsError_None);
	CHECK_FALSE(store.Read(StoreDoc::Settings).isMember("oc_none"));
}

TEST_CASE("Settings_shared_between_instances")
{
	TempDir dir;
	SteamVRFiles files(false);
	SharedStore storeA(dir.path), storeB(dir.path);
	BaseSettings a(storeA), b(storeB);
	BaseSettings::EVRSettingsError error = VRSettingsError_IPCFailed;

	a.SetString("oc_test", "who", "a");
	CHECK(GetString(b, "oc_test", "who", &error) == "a");
	b.SetString("oc_test", "who", "b");
	CHECK(GetString(a, "oc_test", "who", &error) == "b");
	CHECK(a.Sync(true, &error));
	CHECK(error == VRSettingsError_None);
}

TEST_CASE("Settings_supersample_store_beats_ini")
{
	TempDir dir;
	SharedStore store(dir.path);
	BaseSettings settings(store);

	{
		// Nothing set: the ini's, also as the setting
		SteamVRFiles files(false);
		CHECK(BaseSettings::RenderScale(store, 1.25f) == 1.25f);
	}
	{
		// The user's SteamVR file with its manual override on, and the game's percentage: both scale the pixel count, so
		// each axis by their square root
		SteamVRFiles files(true);
		CHECK(BaseSettings::RenderScale(store, 1.25f) == doctest::Approx(std::sqrt(1.5)));
		AppId app("4242");
		CHECK(BaseSettings::RenderScale(store, 1.25f) == doctest::Approx(std::sqrt(1.5 * 0.5)));
		// Read back as SteamVR keeps it: the pixel-count scale, without the app's (it has its own key)
		CHECK(settings.GetFloat("steamvr", "supersampleScale") == doctest::Approx(1.5));
	}
	{
		// A file without the manual override is SteamVR's own pick, not the user's
		TempDir other;
		fs::path user = other.path / "steamvr.vrsettings";
		std::ofstream(user) << "{ \"steamvr\": { \"supersampleScale\": 3.0 } }";
		SteamVRSettingsUseFiles(user, other.path / "none");
		CHECK(BaseSettings::RenderScale(store, 1.25f) == 1.25f);
		SteamVRSettingsUseFiles();
	}
	{
		// The store beats the file and the ini, and the setting reads it back
		SteamVRFiles files(true);
		settings.SetFloat("steamvr", "supersampleScale", 4.0f);
		CHECK(BaseSettings::RenderScale(store, 1.25f) == 2.0f);
		CHECK(settings.GetFloat("steamvr", "supersampleScale") == 4.0f);
		settings.SetFloat("steam.app.4242", "resolutionScale", 225.0f);
		AppId app("4242");
		CHECK(BaseSettings::RenderScale(store, 1.25f) == 3.0f);
		// Nonsense isn't a scale: the ini's instead, still with the app's
		settings.SetFloat("steamvr", "supersampleScale", -1.0f);
		CHECK(BaseSettings::RenderScale(store, 1.25f) == doctest::Approx(1.25 * 1.5));
	}
}

TEST_CASE("Settings_last_known_hmd_agrees_with_properties")
{
	TempDir dir;
	SteamVRFiles files(false);
	SharedStore store(dir.path);
	BaseSettings settings(store);
	BaseSettings::EVRSettingsError error = VRSettingsError_IPCFailed;

	// No session in the tests: the default row, which Prop_ManufacturerName_String answers too
	const std::string none;
	const DeviceIdentity& hmd = HmdIdentity(none);
	CHECK(GetString(settings, "LastKnown", "HMDManufacturer", &error) == hmd.manufacturer);
	CHECK(error == VRSettingsError_None);
	// The default row has no model number; the answer OpenComposite always gave
	CHECK(hmd.modelNumber.empty());
	CHECK(GetString(settings, "LastKnown", "HMDModel", &error) == "Oculus Quest2");
}
