// doctest first: stdafx.h brings in Xlib on Linux, whose macros (None, Bool, ...) clash with doctest's names
#include "doctest.h"

#include "stdafx.h"

#include "Misc/LibMonado.h"
#include "XrController.h"

#ifndef _WIN32
#include <dlfcn.h>
#endif

#include <chrono>
#include <cstdlib>
#include <fstream>

namespace fs = std::filesystem;

namespace {
void SetEnv(const char* name, const char* value)
{
#ifdef _WIN32
	_putenv_s(name, value ? value : "");
#else
	if (value)
		setenv(name, value, 1);
	else
		unsetenv(name);
#endif
}

// The fake library (tests/fakes/fake_libmonado.c) as libmonado, with its scripted answers, for one test
struct FakeLibMonado {
	FakeLibMonado(const char* major, const char* battery)
	{
		SetEnv("OC_TEST_LIBMONADO_PATH", OC_TEST_FAKE_LIBMONADO);
		SetEnv("FAKE_MND_MAJOR", major);
		SetEnv("FAKE_MND_BATTERY", battery);
	}
	~FakeLibMonado()
	{
		SetEnv("OC_TEST_LIBMONADO_PATH", nullptr);
		SetEnv("FAKE_MND_MAJOR", nullptr);
		SetEnv("FAKE_MND_BATTERY", nullptr);
		SetEnv("FAKE_MND_SAME_NAMES", nullptr);
		SetEnv("FAKE_MND_NO_BATTERY", nullptr);
	}
};
} // namespace

TEST_CASE("Battery_from_libmonado")
{
	FakeLibMonado fake("1", "1,1,0.5");
	LibMonado mnd("Monado(XRT) by Collabora et al");
	REQUIRE(mnd.Available());
	std::optional<LibMonado::Battery> b = mnd.BatteryFor("Fake Left", "");
	REQUIRE(b.has_value());
	CHECK(b->present);
	CHECK(b->charging);
	CHECK(b->charge == 0.5f);
	CHECK(mnd.BatteryFor("", "SN2").has_value()); // by serial
	CHECK_FALSE(mnd.BatteryFor("Nobody", "").has_value());
	CHECK(mnd.DeviceForRole("right") == std::optional<std::string>("Fake Right"));
	CHECK(mnd.BatteryForRole("head")->charge == 0.5f);
	CHECK_FALSE(mnd.DeviceForRole("gamepad").has_value());
}

TEST_CASE("Version_call_returning_nothing_accepted")
{
	// libmonado's mnd_api_get_version returns nothing, as the fake's does (built optimised, the fake's leaves its major
	// version, 1, in the return register): only the version it writes counts
	FakeLibMonado fake("1", "1,1,0.5");
	LibMonado mnd("Monado(XRT) by Collabora et al");
	CHECK(mnd.Available());
}

TEST_CASE("Wrong_major_version_unavailable")
{
	FakeLibMonado fake("2", "1,0,0.9");
	LibMonado mnd("Monado(XRT) by Collabora et al");
	CHECK_FALSE(mnd.Available());
	CHECK_FALSE(mnd.BatteryFor("Fake Left", "").has_value());
	CHECK_FALSE(mnd.BatteryForRole("left").has_value());
}

TEST_CASE("Missing_library_keeps_todays_answers")
{
	SetEnv("OC_TEST_LIBMONADO_PATH", (fs::temp_directory_path() / "oc_no_such_libmonado.so").string().c_str());
	LibMonado mnd("Monado(XRT) by Collabora et al");
	SetEnv("OC_TEST_LIBMONADO_PATH", nullptr);
	CHECK_FALSE(mnd.Available());
	CHECK_FALSE(mnd.BatteryForRole("left").has_value());
	vr::HmdMatrix34_t m{};
	CHECK_FALSE(mnd.SetReferenceSpaceOffset(0, m));
}

TEST_CASE("Non_monado_runtime_not_loaded")
{
	FakeLibMonado fake("1", "1,1,0.5");
	LibMonado mnd("SteamVR/OpenXR");
	CHECK_FALSE(mnd.Available());
}

TEST_CASE("Battery_by_role_is_the_roles_device")
{
	// Two controllers with one name (a driver may name them alike): each hand's battery is its own device's
	FakeLibMonado fake("1", "1,0,0.5;1,0,0.25;1,1,0.75");
	SetEnv("FAKE_MND_SAME_NAMES", "1");
	LibMonado mnd("Monado(XRT) by Collabora et al");
	REQUIRE(mnd.Available());
	std::optional<LibMonado::Battery> left = mnd.BatteryForRole("left"), right = mnd.BatteryForRole("right");
	SetEnv("FAKE_MND_SAME_NAMES", nullptr);
	REQUIRE(left.has_value());
	REQUIRE(right.has_value());
	CHECK(left->charge == 0.25f);
	CHECK(right->charge == 0.75f);
	CHECK(right->charging);
}

TEST_CASE("Battery_unsupported_is_no_battery")
{
	// Monado fails the call for a device without battery status: libmonado is there, and the device has none
	FakeLibMonado fake("1", "1,1,0.5");
	SetEnv("FAKE_MND_NO_BATTERY", "1");
	LibMonado mnd("Monado(XRT) by Collabora et al");
	std::optional<LibMonado::Battery> b = mnd.BatteryForRole("left");
	SetEnv("FAKE_MND_NO_BATTERY", nullptr);
	REQUIRE(mnd.Available());
	REQUIRE(b.has_value());
	CHECK_FALSE(b->present);
}

TEST_CASE("Libmonado_bare_name_loaded_by_name")
{
	// Monado's manifests may name the library without a folder (the soname distributions install): the loader's
	// search finds it, not the manifest's folder
	auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
	fs::path dir = fs::temp_directory_path() / ("oc_mnd_bare_" + std::to_string(stamp));
	fs::create_directories(dir);
	fs::path manifest = dir / "openxr_monado.json";
	std::ofstream(manifest) << R"({ "file_format_version": "1.0.0", "runtime": { "library_path": "x.so", "MND_libmonado_path": "libmonado.so.1" } })";
	std::optional<fs::path> lib = LibMonado::LibraryFromManifest(manifest);
	std::error_code ec;
	fs::remove_all(dir, ec);
	REQUIRE(lib.has_value());
	CHECK(*lib == fs::path("libmonado.so.1"));
}

#ifdef _WIN32
TEST_CASE("Runtime_manifest_from_a_non_ascii_variable")
{
	// XR_RUNTIME_JSON with a user folder outside ASCII, read as the wide environment holds it
	const wchar_t* path = L"C:\\Users\\J\u00fcrgen\\monado\\openxr_monado.json";
	_wputenv_s(L"XR_RUNTIME_JSON", path);
	std::optional<fs::path> found = LibMonado::RuntimeManifestFromEnvironment();
	_wputenv_s(L"XR_RUNTIME_JSON", L"");
	REQUIRE(found.has_value());
	CHECK(found->wstring() == std::wstring(path));
}
#endif

TEST_CASE("Libmonado_path_from_manifest")
{
	auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
	fs::path dir = fs::temp_directory_path() / ("oc_mnd_manifest_" + std::to_string(stamp));
	fs::create_directories(dir);
	fs::path manifest = dir / "openxr_monado.json";
	std::ofstream(manifest) << R"({ "file_format_version": "1.0.0", "runtime": { "name": "Monado", "library_path": "bin/openxr_monado.dll", "MND_libmonado_path": "bin/monado.dll" } })";
	std::optional<fs::path> lib = LibMonado::LibraryFromManifest(manifest);
	REQUIRE(lib.has_value());
	CHECK(*lib == (fs::weakly_canonical(dir) / "bin" / "monado.dll").lexically_normal());
	std::ofstream(manifest) << R"({ "file_format_version": "1.0.0", "runtime": { "library_path": "x.dll" } })";
	CHECK_FALSE(LibMonado::LibraryFromManifest(manifest).has_value());
	std::error_code ec;
	fs::remove_all(dir, ec);
}

#ifndef _WIN32
TEST_CASE("Libmonado_path_through_a_chain_of_links")
{
	// active_runtime.json -> a link elsewhere -> the real manifest: the library is next to the real one
	auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
	fs::path dir = fs::weakly_canonical(fs::temp_directory_path()) / ("oc_mnd_links_" + std::to_string(stamp));
	fs::create_directories(dir / "monado");
	fs::create_directories(dir / "config");
	fs::create_directories(dir / "alternatives");
	std::ofstream(dir / "monado" / "openxr_monado.json") << R"({ "file_format_version": "1.0.0", "runtime": { "library_path": "x.so", "MND_libmonado_path": "lib/libmonado.so" } })";
	fs::create_symlink("../monado/openxr_monado.json", dir / "alternatives" / "openxr");
	fs::create_symlink("../alternatives/openxr", dir / "config" / "active_runtime.json");
	std::optional<fs::path> lib = LibMonado::LibraryFromManifest(dir / "config" / "active_runtime.json");
	std::error_code ec;
	fs::remove_all(dir, ec);
	REQUIRE(lib.has_value());
	CHECK(*lib == dir / "monado" / "lib" / "libmonado.so");
}
#endif

namespace {
// The fake's count of battery calls: it's the library LibMonado loaded (the same file, so the same module)
int FakeBatteryCalls()
{
	using Fn = int (*)();
#ifdef _WIN32
	HMODULE lib = LoadLibraryA(OC_TEST_FAKE_LIBMONADO);
	Fn fn = lib ? (Fn)GetProcAddress(lib, "fake_mnd_battery_calls") : nullptr;
#else
	void* lib = dlopen(OC_TEST_FAKE_LIBMONADO, RTLD_NOW);
	Fn fn = lib ? (Fn)dlsym(lib, "fake_mnd_battery_calls") : nullptr;
#endif
	REQUIRE(fn);
	return fn();
}
} // namespace

TEST_CASE("Bool_property_makes_no_battery_call")
{
	FakeLibMonado fake("1", "1,1,0.5");
	LibMonado mnd("Monado(XRT) by Collabora et al");
	REQUIRE(mnd.Available());

	int before = FakeBatteryCalls();
	CHECK_FALSE(XrController::BatteryBoolProperty(mnd, vr::Prop_DeviceIsWireless_Bool, "left").has_value());
	CHECK_FALSE(XrController::BatteryBoolProperty(mnd, vr::Prop_DeviceCanPowerOff_Bool, "right").has_value());
	CHECK(FakeBatteryCalls() == before);

	// The battery's own properties ask for it (LibMonado keeps the answer a while, so once here)
	CHECK(XrController::BatteryBoolProperty(mnd, vr::Prop_DeviceProvidesBatteryStatus_Bool, "left") == std::optional<bool>(true));
	CHECK(XrController::BatteryBoolProperty(mnd, vr::Prop_DeviceIsCharging_Bool, "left") == std::optional<bool>(true));
	int asked = FakeBatteryCalls();
	CHECK(asked > before);
	// No role (a tracker): nothing asked
	CHECK(XrController::BatteryBoolProperty(mnd, vr::Prop_DeviceProvidesBatteryStatus_Bool, nullptr) == std::optional<bool>(true));
	CHECK(FakeBatteryCalls() == asked);
}
