// doctest first: stdafx.h brings in Xlib on Linux, whose macros (None, Bool, ...) clash with doctest's names
#include "doctest.h"

#include "stdafx.h"

#include "Misc/Input/Bindings/BindingFile.h"
#include "Misc/Input/Bindings/Modes.h"
#include "Misc/Numbers.h"
#include "Misc/SharedStore.h"
#include "Misc/SteamVRSettings.h"
#include "Reimpl/BaseSettings.h"

#include <chrono>
#include <clocale>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {
// LC_NUMERIC set to a locale with a decimal comma, as a game may set it, and put back at the end
struct GermanNumbers {
	std::string saved;
	bool set = false;
	GermanNumbers()
	{
		const char* old = std::setlocale(LC_NUMERIC, nullptr);
		saved = old ? old : "C";
		for (const char* name : { "de-DE", "de_DE.UTF-8", "de_DE.utf8", "de_DE" }) {
			if (std::setlocale(LC_NUMERIC, name)) {
				set = true;
				break;
			}
		}
	}
	~GermanNumbers() { std::setlocale(LC_NUMERIC, saved.c_str()); }
};

struct TempDir {
	fs::path path;
	TempDir()
	{
		auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		path = fs::temp_directory_path() / ("oc_numbers_" + std::to_string(stamp));
		fs::create_directories(path);
	}
	~TempDir()
	{
		std::error_code ec;
		fs::remove_all(path, ec);
	}
};
} // namespace

TEST_CASE("Parse_ignores_locale")
{
	GermanNumbers german;
	if (german.set) {
		// The C library now reads a decimal comma
		CHECK(std::strtof("0.55", nullptr) != 0.55f);
	} else {
		MESSAGE("no German locale here: only the C locale is checked");
	}

	CHECK(Numbers::ParseFloat("0.55") == 0.55f);
	CHECK(Numbers::ParseDouble(" +0.55x") == 0.55);
	CHECK(Numbers::ToFloat("0.55", 0) == 0.55f);
	CHECK(Numbers::ToFloat("x", 2.5f) == 2.5f);
	CHECK(Numbers::ToFloat("1e99", 2.5f) == 2.5f);
	CHECK(Numbers::ToFloat(std::wstring_view(L"1.5"), 0) == 1.5f);
	CHECK(Numbers::FormatGeneral(0.6, 9) == "0.6");
	CHECK(Numbers::FormatFixed(1.25, 6) == "1.250000");
	CHECK_FALSE(Numbers::ParseFloat("x"));

	// A binding mode's parameter
	SourceBinding b;
	b.path = "/user/hand/right/input/trigger";
	b.mode = "scalar_constant";
	b.inputs = { { "value", "/speed" } };
	b.parameters = { { "value", "0.55" } };
	std::vector<std::string> warnings;
	std::unique_ptr<ModeBinding> mode = CreateModeBinding(b, warnings);
	REQUIRE(mode);
	RawInputState in;
	ActionOutputs out;
	mode->Update(in, out);
	CHECK(out.analog["/speed"].x == 0.55f);

	// A binding file's number parameter, kept as text
	BindingFile f = LoadBindingFile(fs::path(OC_TEST_DATA_DIR) / "bindings_all_sections.json");
	REQUIRE(f.sets.count("/actions/main"));
	CHECK(f.sets["/actions/main"].sources.at(0).parameters.at("click_deactivate_threshold") == "0.6");

	// Settings: text read as a number, and a number read as text
	TempDir dir;
	fs::path none = fs::temp_directory_path() / "oc_numbers_no_such_file.vrsettings";
	SteamVRSettingsUseFiles(none, none);
	{
		SharedStore store(dir.path);
		BaseSettings settings(store);
		BaseSettings::EVRSettingsError error = VRSettingsError_IPCFailed;
		settings.SetString("oc_test", "number", "0.55", &error);
		CHECK(settings.GetFloat("oc_test", "number", &error) == 0.55f);
		settings.SetFloat("oc_test", "scale", 1.25f, &error);
		char buff[64] = {};
		settings.GetString("oc_test", "scale", buff, sizeof(buff), &error);
		CHECK(std::string(buff) == "1.250000");
	}
	SteamVRSettingsUseFiles();
}
