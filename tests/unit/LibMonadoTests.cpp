// doctest first: stdafx.h brings in Xlib on Linux, whose macros (None, Bool, ...) clash with doctest's names
#include "doctest.h"

#include "stdafx.h"

#include "Misc/LibMonado.h"

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

TEST_CASE("Libmonado_path_from_manifest")
{
	auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
	fs::path dir = fs::temp_directory_path() / ("oc_mnd_manifest_" + std::to_string(stamp));
	fs::create_directories(dir);
	fs::path manifest = dir / "openxr_monado.json";
	std::ofstream(manifest) << R"({ "file_format_version": "1.0.0", "runtime": { "name": "Monado", "library_path": "bin/openxr_monado.dll", "MND_libmonado_path": "bin/monado.dll" } })";
	std::optional<fs::path> lib = LibMonado::LibraryFromManifest(manifest);
	REQUIRE(lib.has_value());
	CHECK(*lib == (dir / "bin" / "monado.dll").lexically_normal());
	std::ofstream(manifest) << R"({ "file_format_version": "1.0.0", "runtime": { "library_path": "x.dll" } })";
	CHECK_FALSE(LibMonado::LibraryFromManifest(manifest).has_value());
	std::error_code ec;
	fs::remove_all(dir, ec);
}
