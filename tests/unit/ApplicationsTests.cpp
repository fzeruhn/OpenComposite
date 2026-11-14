// doctest first: stdafx.h brings in Xlib on Linux, whose macros (None, Bool, ...) clash with doctest's names
#include "doctest.h"

#include "stdafx.h"

#include "Misc/AppManifest.h"
#include "Misc/Process.h"
#include "Misc/SharedStore.h"
#include "Reimpl/BaseApplications.h"
#include "Reimpl/BaseClientCore.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {
// A fresh folder under the system's temporary one (named sub, if given), removed again at the end
struct TempDir {
	fs::path root, path;
	explicit TempDir(const fs::path& sub = {})
	{
		auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		root = fs::temp_directory_path() / ("oc_apps_" + std::to_string(stamp));
		path = sub.empty() ? root : root / sub;
		fs::create_directories(path);
	}
	~TempDir()
	{
		std::error_code ec;
		fs::remove_all(root, ec);
	}
};

fs::path Data(const char* name)
{
	return fs::path(OC_TEST_DATA_DIR) / name;
}

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

// The id of a process that has started and ended
uint32_t ExitedProcessId()
{
#ifdef _WIN32
	wchar_t cmd[] = L"cmd.exe /c exit 0";
	STARTUPINFOW si{};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi{};
	REQUIRE(CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi));
	WaitForSingleObject(pi.hProcess, INFINITE);
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess); // the last handle: the id is the ended process's no more
	return (uint32_t)pi.dwProcessId;
#else
	pid_t pid = fork();
	if (pid == 0)
		_exit(0);
	REQUIRE(pid > 0);
	int status = 0;
	waitpid(pid, &status, 0);
	return (uint32_t)pid;
#endif
}

std::string KeyString(BaseApplications& apps, uint32_t index)
{
	char key[128] = "unchanged";
	apps.GetApplicationKeyByIndex(index, key, sizeof(key));
	return key;
}
} // namespace

TEST_CASE("Manifest_parses_fields")
{
	std::vector<std::string> warnings;
	std::vector<AppManifestEntry> apps = ParseAppManifest(Data("overlay_apps.vrmanifest"), warnings);
	REQUIRE(apps.size() == 2);
	CHECK(warnings.size() == 1); // the application without a key

	const AppManifestEntry& overlay = apps[0];
	CHECK(overlay.appKey == "oc.test.overlay");
	CHECK(overlay.launchType == "binary");
	CHECK(overlay.arguments == "--overlay");
	CHECK(overlay.isDashboardOverlay);
	CHECK_FALSE(overlay.isTemplate);
	CHECK(overlay.nameEnUs == "Overlay Tool");
	CHECK(overlay.descriptionEnUs == "A dashboard overlay");
	CHECK(overlay.source == "oc_tests");
	CHECK(overlay.ResolvedBinary() == (fs::absolute(Data("bin")) / "overlay_tool").lexically_normal());
	CHECK(overlay.ResolvedWorkingDirectory() == fs::absolute(Data("bin")).lexically_normal());
	CHECK(overlay.Resolved(overlay.actionManifestPath) == fs::absolute(Data("actions.json")).lexically_normal());

	const AppManifestEntry& templ = apps[1];
	CHECK(templ.launchType == "url");
	CHECK(templ.url == "https://example.invalid/app");
	CHECK(templ.isTemplate);

	// The platform's binary
	std::vector<AppManifestEntry> echo = ParseAppManifest(Data("echo.vrmanifest"), warnings);
	REQUIRE(echo.size() == 1);
#ifdef _WIN32
	CHECK(echo[0].binaryPath == "oc_echo_app.exe");
#else
	CHECK(echo[0].binaryPath == "oc_echo_app");
#endif
	CHECK(echo[0].mimeTypes == std::vector<std::string>{ "vr/oc-echo" });

	// Through the store and back
	AppManifestEntry back = AppManifestEntry::FromJson(overlay.ToJson());
	CHECK(back.appKey == overlay.appKey);
	CHECK(back.ResolvedBinary() == overlay.ResolvedBinary());
	CHECK(back.isDashboardOverlay);

	// No such file, or not a manifest
	warnings.clear();
	CHECK(ParseAppManifest(Data("no_such.vrmanifest"), warnings).empty());
	CHECK(warnings.size() == 1);
	CHECK(ParseAppManifest(Data("steamvr.vrsettings"), warnings).empty());
}

TEST_CASE("Manifest_with_non_ascii_path")
{
	TempDir dir(fs::path(u8"Jürgen_テスト") / u8"Spiele");
	fs::path manifest = dir.path / u8"spiel.vrmanifest";
	fs::copy_file(Data("echo.vrmanifest"), manifest);

	SharedStore store(dir.root / "store");
	BaseApplications apps(store);
	CHECK(apps.AddApplicationManifest(PathToUtf8(manifest).c_str()) == VRApplicationError_None);
	CHECK(apps.IsApplicationInstalled("oc.test.echo"));
	char binary[512];
	BaseApplications::EVRApplicationError error;
	apps.GetApplicationPropertyString("oc.test.echo", VRApplicationProperty_BinaryPath_String, binary, sizeof(binary), &error);
	CHECK(error == VRApplicationError_None);
	CHECK(PathFromUtf8(binary).parent_path() == dir.path.lexically_normal());
}

TEST_CASE("Applications_registry_calls")
{
	TempDir dir;
	SharedStore store(dir.path);
	BaseApplications apps(store);
	BaseApplications::EVRApplicationError error;

	CHECK(apps.GetApplicationCount() == 0);
	CHECK(apps.AddApplicationManifest(PathToUtf8(Data("overlay_apps.vrmanifest")).c_str()) == VRApplicationError_None);
	CHECK(apps.AddApplicationManifest(PathToUtf8(Data("echo.vrmanifest")).c_str()) == VRApplicationError_None);
	CHECK(apps.AddApplicationManifest(PathToUtf8(Data("steamvr.vrsettings")).c_str()) == VRApplicationError_InvalidManifest);
	CHECK(apps.GetApplicationCount() == 3);
	CHECK(KeyString(apps, 0) == "oc.test.echo"); // by key
	CHECK(KeyString(apps, 2) == "oc.test.template");
	char key[4];
	CHECK(apps.GetApplicationKeyByIndex(0, key, sizeof(key)) == VRApplicationError_BufferTooSmall);
	CHECK(apps.GetApplicationKeyByIndex(3, key, sizeof(key)) == VRApplicationError_InvalidIndex);

	// Properties
	char name[64];
	CHECK(apps.GetApplicationPropertyString("oc.test.echo", VRApplicationProperty_Name_String, name, sizeof(name), &error) == 5);
	CHECK(std::string(name) == "Echo");
	CHECK(error == VRApplicationError_None);
	CHECK(apps.GetApplicationPropertyString("oc.test.echo", VRApplicationProperty_Name_String, name, 3, &error) == 5);
	CHECK(error == VRApplicationError_BufferTooSmall);
	apps.GetApplicationPropertyString("oc.test.echo", VRApplicationProperty_URL_String, name, sizeof(name), &error);
	CHECK(error == VRApplicationError_PropertyNotSet);
	CHECK(apps.GetApplicationPropertyBool("oc.test.overlay", VRApplicationProperty_IsDashboardOverlay_Bool, &error));
	CHECK(error == VRApplicationError_None);
	CHECK(apps.GetApplicationPropertyBool("oc.test.template", VRApplicationProperty_IsTemplate_Bool, &error));
	CHECK_FALSE(apps.GetApplicationPropertyBool("oc.none", VRApplicationProperty_IsTemplate_Bool, &error));
	CHECK(error == VRApplicationError_UnknownApplication);
	CHECK(apps.GetApplicationPropertyUint64("oc.test.echo", VRApplicationProperty_LastLaunchTime_Uint64, &error) == 0);

	// The same key from another manifest
	TempDir other;
	fs::path copy = other.path / "copy.vrmanifest";
	fs::copy_file(Data("echo.vrmanifest"), copy);
	CHECK(apps.AddApplicationManifest(PathToUtf8(copy).c_str()) == VRApplicationError_AppKeyAlreadyExists);

	// Removed again
	CHECK(apps.RemoveApplicationManifest(PathToUtf8(Data("overlay_apps.vrmanifest")).c_str()) == VRApplicationError_None);
	CHECK(apps.GetApplicationCount() == 1);
	CHECK_FALSE(apps.IsApplicationInstalled("oc.test.overlay"));

	// Another program sees the same registry
	SharedStore store2(dir.path);
	BaseApplications apps2(store2);
	CHECK(apps2.IsApplicationInstalled("oc.test.echo"));
	CHECK(apps2.IdentifyApplication(0, "oc.test.echo") == VRApplicationError_None);
	CHECK(CurrentProcessAppKey(store) == "oc.test.echo");
	CHECK(apps.IdentifyApplication(0, "") == VRApplicationError_UnknownApplication);
	// A key without a manifest is taken too
	CHECK(apps.IdentifyApplication(0, "steam.app.620980") == VRApplicationError_None);
	CHECK(CurrentProcessAppKey(store) == "steam.app.620980");
}

TEST_CASE("Temporary_manifest_goes_with_its_process")
{
	TempDir dir;
	SharedStore store(dir.path);
	BaseApplications apps(store);

	// Ours, while we run
	CHECK(apps.AddApplicationManifest(PathToUtf8(Data("echo.vrmanifest")).c_str(), true) == VRApplicationError_None);
	CHECK(apps.IsApplicationInstalled("oc.test.echo"));

	// A child's that has exited: gone
	uint32_t dead = ExitedProcessId();
	store.Update(StoreDoc::Applications, [&](Json::Value& doc) {
		for (const std::string& path : doc["manifests"].getMemberNames())
			doc["manifests"][path]["owner_pid"] = dead;
	});
	CHECK_FALSE(apps.IsApplicationInstalled("oc.test.echo"));
	CHECK(apps.GetApplicationCount() == 0);
}

TEST_CASE("Key_from_SteamAppId")
{
	TempDir dir;
	SharedStore store(dir.path);
	{
		AppId id("620980");
		CHECK(CurrentProcessAppKey(store) == "steam.app.620980");
	}
	// Without it, a key of its own from the program's name
	std::string key = CurrentProcessAppKey(store);
	CHECK(key.rfind("system.generated.oc_unit_tests", 0) == 0);
	BaseApplications apps(store);
	char buffer[128];
	CHECK(apps.GetApplicationKeyByProcessId(oc_current_pid(), buffer, sizeof(buffer)) == VRApplicationError_None);
	CHECK(std::string(buffer) == key);
	CHECK(apps.GetApplicationProcessId(key.c_str()) == oc_current_pid());
}

TEST_CASE("Autolaunch_kept")
{
	TempDir dir;
	SharedStore store(dir.path);
	BaseApplications apps(store);
	CHECK(apps.SetApplicationAutoLaunch("oc.test.overlay", true) == VRApplicationError_UnknownApplication);
	apps.AddApplicationManifest(PathToUtf8(Data("overlay_apps.vrmanifest")).c_str());
	CHECK_FALSE(apps.GetApplicationAutoLaunch("oc.test.overlay"));
	CHECK(apps.SetApplicationAutoLaunch("oc.test.overlay", true) == VRApplicationError_None);
	SharedStore store2(dir.path);
	CHECK(BaseApplications(store2).GetApplicationAutoLaunch("oc.test.overlay"));
	CHECK(apps.SetApplicationAutoLaunch("oc.test.overlay", false) == VRApplicationError_None);
	CHECK_FALSE(apps.GetApplicationAutoLaunch("oc.test.overlay"));
}

TEST_CASE("Mime_default_kept")
{
	TempDir dir;
	SharedStore store(dir.path);
	BaseApplications apps(store);
	apps.AddApplicationManifest(PathToUtf8(Data("overlay_apps.vrmanifest")).c_str());
	apps.AddApplicationManifest(PathToUtf8(Data("echo.vrmanifest")).c_str());
	char buffer[128];

	// From the manifest
	CHECK(apps.GetDefaultApplicationForMimeType("vr/oc-echo", buffer, sizeof(buffer)));
	CHECK(std::string(buffer) == "oc.test.echo");
	CHECK(apps.GetApplicationSupportedMimeTypes("oc.test.echo", buffer, sizeof(buffer)));
	CHECK(std::string(buffer) == "vr/oc-echo");
	CHECK_FALSE(apps.GetDefaultApplicationForMimeType("vr/nobody", buffer, sizeof(buffer)));

	// Set, and kept for every program
	CHECK(apps.SetDefaultApplicationForMimeType("oc.test.overlay", "vr/oc-echo") == VRApplicationError_None);
	SharedStore store2(dir.path);
	BaseApplications apps2(store2);
	CHECK(apps2.GetDefaultApplicationForMimeType("vr/oc-echo", buffer, sizeof(buffer)));
	CHECK(std::string(buffer) == "oc.test.overlay");
	CHECK(apps2.GetApplicationSupportedMimeTypes("oc.test.overlay", buffer, sizeof(buffer)));
	CHECK(std::string(buffer) == "vr/oc-echo");
	CHECK(apps2.GetApplicationsThatSupportMimeType("vr/oc-echo", buffer, sizeof(buffer)) == strlen("oc.test.echo,oc.test.overlay") + 1);
	CHECK(std::string(buffer) == "oc.test.echo,oc.test.overlay");
	CHECK(apps.SetDefaultApplicationForMimeType("oc.none", "vr/oc-echo") == VRApplicationError_UnknownApplication);
}

TEST_CASE("Registry_values_of_another_type_read_as_unset")
{
	TempDir dir;
	SharedStore store(dir.path);
	BaseApplications apps(store);
	BaseApplications::EVRApplicationError error;
	apps.AddApplicationManifest(PathToUtf8(Data("echo.vrmanifest")).c_str());
	CHECK(store.Update(StoreDoc::Applications, [](Json::Value& doc) {
		doc["autolaunch"]["oc.test.echo"] = "yes";
		doc["last_launch"]["oc.test.echo"] = "today";
		doc["mime_defaults"]["vr/oc-echo"] = Json::Value(Json::objectValue);
		doc["pids"]["1"] = Json::Value(Json::arrayValue);
	}));
	CHECK(store.Update(StoreDoc::Scene, [](Json::Value& scene) { scene["app_key"] = Json::Value(Json::objectValue); }));

	char buffer[128];
	CHECK_FALSE(apps.GetApplicationAutoLaunch("oc.test.echo"));
	CHECK(apps.GetApplicationPropertyUint64("oc.test.echo", VRApplicationProperty_LastLaunchTime_Uint64, &error) == 0);
	CHECK(error == VRApplicationError_None);
	CHECK(apps.GetDefaultApplicationForMimeType("vr/oc-echo", buffer, sizeof(buffer)));
	CHECK(std::string(buffer) == "oc.test.echo");
	CHECK(apps.GetApplicationSupportedMimeTypes("oc.test.echo", buffer, sizeof(buffer)));
	CHECK(apps.GetApplicationProcessId("oc.none") == 0);
}

TEST_CASE("Launch_unknown_key_errors")
{
	TempDir dir;
	SharedStore store(dir.path);
	BaseApplications apps(store);
	CHECK(apps.LaunchApplication("oc.none") == VRApplicationError_UnknownApplication);
	CHECK(apps.LaunchApplication(nullptr) == VRApplicationError_UnknownApplication);
	CHECK(apps.PerformApplicationPrelaunchCheck("oc.none") == VRApplicationError_UnknownApplication);

	apps.AddApplicationManifest(PathToUtf8(Data("overlay_apps.vrmanifest")).c_str());
	CHECK(apps.LaunchApplication("oc.test.template") == VRApplicationError_IsTemplate);
	CHECK(apps.LaunchApplication("oc.test.overlay") == VRApplicationError_InvalidApplication); // a dashboard overlay
	CHECK(apps.LaunchDashboardOverlay("oc.test.overlay") == VRApplicationError_LaunchFailed); // its binary isn't there
	CHECK(apps.LaunchTemplateApplication("oc.test.overlay", "oc.new", nullptr, 0) == VRApplicationError_InvalidApplication);
	CHECK(apps.PerformApplicationPrelaunchCheck("oc.test.overlay") == VRApplicationError_None);
}

TEST_CASE("Command_line_split_as_CommandLineToArgvW")
{
	using V = std::vector<std::string>;
	CHECK(SplitCommandLine("") == V{});
	CHECK(SplitCommandLine("  a\tb  ") == V{ "a", "b" });
	CHECK(SplitCommandLine("\"a b\" c\"d e\"f \"\"") == V{ "a b", "cd ef", "" });
	CHECK(SplitCommandLine("& $HOME ; `x` | > *") == V{ "&", "$HOME", ";", "`x`", "|", ">", "*" });
	CHECK(SplitCommandLine("a\\b \\\\\"c d\" \\\"e") == V{ "a\\b", "\\c d", "\"e" });
	CHECK(SplitCommandLine("\"a \"\"b\"") == V{ "a \"b" });
	for (const std::string& arg : { std::string("plain"), std::string("with space"), std::string("q\"uote"),
	         std::string("trail\\"), std::string("back\\\"slash"), std::string("") })
		CHECK(SplitCommandLine("x " + QuoteArgument(arg) + " y") == V{ "x", arg, "y" });
}

TEST_CASE("Launch_without_a_shell")
{
	TempDir dir("with space");
	// The marker named relative to the working folder; nothing of it read by a shell
	std::optional<uint32_t> pid = LaunchProcess(fs::path(OC_ECHO_APP), "\"marker file.txt\" a & $HOME ;b `c` \"d e\"", dir.path);
	REQUIRE(pid);

	// It ends, and isn't left a zombie that seems to run
	bool ended = false;
	for (int i = 0; i < 50 && !ended; i++) {
		ended = !SharedStore::IsProcessAlive(*pid);
		if (!ended)
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	CHECK(ended);
	std::ifstream in(dir.path / "marker file.txt");
	std::string line;
	std::getline(in, line);
	CHECK(line == "a & $HOME ;b `c` d e");
}

TEST_CASE("Ended_programs_dropped_on_every_update")
{
	TempDir dir;
	SharedStore store(dir.path);
	BaseApplications apps(store);
	REQUIRE(apps.AddApplicationManifest(PathToUtf8(Data("echo.vrmanifest")).c_str()) == VRApplicationError_None);
	uint32_t dead = ExitedProcessId();
	store.Update(StoreDoc::Applications, [&](Json::Value& doc) {
		doc["pids"][std::to_string(dead)] = "oc.test.echo";
		Json::Value& m = doc["manifests"]["template:oc.gone"];
		m["temporary"] = true;
		m["owner_pid"] = dead;
		m["apps"] = Json::Value(Json::arrayValue);
	});

	// Any change to the document
	CHECK(apps.SetApplicationAutoLaunch("oc.test.echo", true) == VRApplicationError_None);
	Json::Value doc = store.Read(StoreDoc::Applications);
	CHECK_FALSE(doc["pids"].isMember(std::to_string(dead)));
	CHECK_FALSE(doc["manifests"].isMember("template:oc.gone"));
	CHECK(apps.IsApplicationInstalled("oc.test.echo"));

	// Values of the wrong type, as a broken file may have: no temporary manifest, no process id
	store.Update(StoreDoc::Applications, [&](Json::Value& d) {
		for (const std::string& path : d["manifests"].getMemberNames()) {
			d["manifests"][path]["temporary"] = "yes";
			d["manifests"][path]["owner_pid"] = -1;
		}
	});
	CHECK(apps.IsApplicationInstalled("oc.test.echo"));
	CHECK(apps.SetApplicationAutoLaunch("oc.test.echo", false) == VRApplicationError_None);
}

TEST_CASE("Manifest_whose_file_is_gone_is_replaced")
{
	TempDir dir;
	SharedStore store(dir.path / "store");
	BaseApplications apps(store);
	fs::create_directories(dir.path / "old");
	fs::create_directories(dir.path / "new");
	fs::path old = dir.path / "old" / "echo.vrmanifest", moved = dir.path / "new" / "echo.vrmanifest";
	fs::copy_file(Data("echo.vrmanifest"), old);
	fs::copy_file(Data("echo.vrmanifest"), moved);
	CHECK(apps.AddApplicationManifest(PathToUtf8(old).c_str()) == VRApplicationError_None);
	CHECK(apps.AddApplicationManifest(PathToUtf8(moved).c_str()) == VRApplicationError_AppKeyAlreadyExists);

	// The game moved
	fs::remove(old);
	CHECK(apps.AddApplicationManifest(PathToUtf8(moved).c_str()) == VRApplicationError_None);
	CHECK(apps.GetApplicationCount() == 1);
	char binary[512];
	BaseApplications::EVRApplicationError error;
	apps.GetApplicationPropertyString("oc.test.echo", VRApplicationProperty_BinaryPath_String, binary, sizeof(binary), &error);
	CHECK(PathFromUtf8(binary).parent_path() == (dir.path / "new").lexically_normal());
}

TEST_CASE("Manifest_by_another_name_is_the_same_manifest")
{
	TempDir dir;
	SharedStore store(dir.path / "store");
	BaseApplications apps(store);
	fs::path first = dir.path / "echo.vrmanifest", second = dir.path / "echo_link.vrmanifest";
	fs::copy_file(Data("echo.vrmanifest"), first);
	// Another path to the same file, as a different case or an 8.3 name is on Windows
	fs::create_hard_link(first, second);
	CHECK(apps.AddApplicationManifest(PathToUtf8(first).c_str()) == VRApplicationError_None);
	CHECK(apps.AddApplicationManifest(PathToUtf8(second).c_str()) == VRApplicationError_None);
	CHECK(apps.GetApplicationCount() == 1);
}

TEST_CASE("Scene_process_is_this_one_when_it_is_a_scene_app")
{
	TempDir dir;
	SharedStore store(dir.path);
	BaseApplications apps(store);
#ifdef _WIN32
	uint32_t other = 4; // System, always there
#else
	uint32_t other = (uint32_t)getppid();
#endif
	store.Update(StoreDoc::Scene, [&](Json::Value& scene) {
		scene["pid"] = other;
		scene["app_key"] = "oc.other";
	});
	vr::EVRApplicationType saved = BaseClientCore::appType;
	BaseClientCore::appType = vr::VRApplication_Utility;
	CHECK(apps.GetCurrentSceneProcessId() == other);
	BaseClientCore::appType = vr::VRApplication_Scene;
	CHECK(apps.GetCurrentSceneProcessId() == oc_current_pid());
	BaseClientCore::appType = saved;
}
