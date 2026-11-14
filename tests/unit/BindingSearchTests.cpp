// doctest first: stdafx.h brings in Xlib on Linux, whose macros (None, Bool, ...) clash with doctest's names
#include "doctest.h"

#include "stdafx.h"

#include "Misc/Input/Bindings/BindingSearch.h"

#include <chrono>
#include <fstream>

namespace fs = std::filesystem;

namespace {
struct TempDir {
	fs::path root;
	TempDir()
	{
		auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		root = fs::temp_directory_path() / ("oc_binding_search_" + std::to_string(stamp));
		fs::create_directories(root);
	}
	~TempDir()
	{
		std::error_code ec;
		fs::remove_all(root, ec);
	}
	fs::path Touch(const fs::path& rel) const
	{
		fs::path p = root / rel;
		fs::create_directories(p.parent_path());
		std::ofstream(p) << "{}";
		return p;
	}
};
} // namespace

TEST_CASE("User_bindings_folder_wins")
{
	TempDir t;
	fs::path user = t.Touch("user/steam.app.42/knuckles.json");
	t.Touch("game/OpenComposite/knuckles.json");
	t.Touch("game/bindings/knuckles.json");
	auto found = FindBindingFile("steam.app.42", "knuckles", t.root / "user", t.root / "game", t.root / "game", "bindings/knuckles.json");
	REQUIRE(found.has_value());
	CHECK(*found == user);
}

TEST_CASE("Working_folder_next")
{
	TempDir t;
	fs::path local = t.Touch("game/OpenComposite/knuckles.json");
	t.Touch("game/bindings/knuckles.json");
	t.Touch("user/steam.app.42/oculus_touch.json"); // another controller's
	auto found = FindBindingFile("steam.app.42", "knuckles", t.root / "user", t.root / "game", t.root / "game", "bindings/knuckles.json");
	REQUIRE(found.has_value());
	CHECK(*found == local);
}

TEST_CASE("Default_last")
{
	TempDir t;
	fs::path def = t.Touch("game/bindings/knuckles.json");
	auto found = FindBindingFile("steam.app.42", "knuckles", t.root / "user", t.root / "game", t.root / "game", "bindings/knuckles.json");
	REQUIRE(found.has_value());
	CHECK(*found == def);
	CHECK_FALSE(FindBindingFile("steam.app.42", "knuckles", t.root / "user", t.root / "game", t.root / "game", "bindings/none.json").has_value());
	CHECK_FALSE(FindBindingFile("steam.app.42", "knuckles", t.root / "user", t.root / "game", t.root / "game", "").has_value());
}

TEST_CASE("User_bindings_from_non_ascii_folder")
{
	TempDir t;
	fs::path user = t.Touch(fs::path(u8"Jürgen_テスト") / "bindings" / "steam.app.42" / "knuckles.json");
	auto found = FindBindingFile("steam.app.42", "knuckles", t.root / fs::path(u8"Jürgen_テスト") / "bindings", {}, {}, "");
	REQUIRE(found.has_value());
	CHECK(*found == user);
}

TEST_CASE("App_key_with_dots_is_one_folder")
{
	TempDir t;
	fs::path user = t.Touch("user/system.generated.game.exe/knuckles.json");
	auto found = FindBindingFile("system.generated.game.exe", "knuckles", t.root / "user", {}, {}, "");
	REQUIRE(found.has_value());
	CHECK(*found == user);
	// A key that would leave the folder isn't followed
	t.Touch("knuckles.json");
	CHECK_FALSE(FindBindingFile("..", "knuckles", t.root / "user", {}, {}, "").has_value());
	CHECK_FALSE(FindBindingFile("../x", "knuckles", t.root / "user", {}, {}, "").has_value());
}

#ifdef _WIN32
TEST_CASE("Bindings_dir_from_wide_env")
{
	// A folder name outside the ANSI code page, as the variable holds it (wide)
	const std::wstring dir = L"C:\\oc_bindings_\u0436\u00e9";
	_wputenv_s(L"OC_BINDINGS_DIR", dir.c_str());
	fs::path got = UserBindingsDir();
	_wputenv_s(L"OC_BINDINGS_DIR", L"");
	CHECK(got == fs::path(dir));
}
#endif
