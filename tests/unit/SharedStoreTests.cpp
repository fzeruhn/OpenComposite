// doctest first: stdafx.h brings in Xlib on Linux, whose macros (None, Bool, ...) clash with doctest's names
#include "doctest.h"

#include "stdafx.h"

#include "Misc/Process.h"
#include "Misc/SharedStore.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {
// A fresh folder under the system's temporary one (named sub, if given), removed again at the end
struct TempDir {
	fs::path root, path;
	explicit TempDir(const fs::path& sub = {})
	{
		auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		root = fs::temp_directory_path() / ("oc_store_" + std::to_string(stamp));
		path = sub.empty() ? root : root / sub;
		fs::create_directories(path);
	}
	~TempDir()
	{
		std::error_code ec;
		fs::remove_all(root, ec);
	}
};

std::string Contents(const fs::path& p)
{
	std::ifstream in(p, std::ios::binary);
	std::stringstream s;
	s << in.rdbuf();
	return s.str();
}
} // namespace

TEST_CASE("Store_round_trip")
{
	TempDir dir;
	SharedStore store(dir.path);
	CHECK(store.Read(StoreDoc::Settings).empty());
	CHECK(store.Update(StoreDoc::Settings, [](Json::Value& data) { data["supersample"] = 1.5; }));
	CHECK(store.Read(StoreDoc::Settings)["supersample"].asDouble() == 1.5);
	CHECK(store.Read(StoreDoc::Applications).empty()); // one document doesn't touch another
	CHECK(fs::exists(dir.path / "settings.json"));
}

TEST_CASE("Store_two_instances_see_each_others_updates")
{
	TempDir dir;
	SharedStore a(dir.path), b(dir.path);
	uint64_t before = b.Generation(StoreDoc::Chaperone);
	REQUIRE(a.Update(StoreDoc::Chaperone, [](Json::Value& data) { data["height"] = 2; }));
	CHECK(b.Generation(StoreDoc::Chaperone) != before);
	CHECK(b.Read(StoreDoc::Chaperone)["height"].asInt() == 2);
	REQUIRE(b.Update(StoreDoc::Chaperone, [](Json::Value& data) { data["height"] = 3; }));
	CHECK(a.Read(StoreDoc::Chaperone)["height"].asInt() == 3);
}

TEST_CASE("Store_concurrent_updates_lose_nothing")
{
	TempDir dir;
	SharedStore a(dir.path), b(dir.path);
	std::vector<std::thread> threads;
	// An update may be refused (false: the lock not free within its wait, or the file held open by a scanner) and is
	// then tried again; one it reported done must never be lost to another's
	std::atomic<int> refused{ 0 }, gaveUp{ 0 };
	for (SharedStore* store : { &a, &b }) {
		for (int t = 0; t < 4; t++) {
			threads.emplace_back([store, &refused, &gaveUp] {
				for (int i = 0; i < 1000; i++) {
					int tries = 0;
					while (!store->Update(StoreDoc::Scene, [](Json::Value& data) { data["count"] = data["count"].asInt() + 1; })) {
						refused++;
						if (++tries == 20) {
							gaveUp++;
							break;
						}
					}
				}
			});
		}
	}
	for (auto& t : threads)
		t.join();
	MESSAGE("updates refused and tried again: " << refused.load());
	REQUIRE(gaveUp == 0);
	CHECK(a.Read(StoreDoc::Scene)["count"].asInt() == 8000);
	CHECK(b.Read(StoreDoc::Scene)["count"].asInt() == 8000);
}

TEST_CASE("Store_survives_corrupt_document")
{
	TempDir dir;
	std::ofstream(dir.path / "settings.json") << "{ this isn't JSON";
	SharedStore store(dir.path);
	CHECK(store.Read(StoreDoc::Settings).empty());
	CHECK(store.Update(StoreDoc::Settings, [](Json::Value& data) { data["ok"] = true; }));
	CHECK(store.Read(StoreDoc::Settings)["ok"].asBool());
	CHECK(fs::exists(dir.path / "settings.json.bad"));
	CHECK(Contents(dir.path / "settings.json.bad") == "{ this isn't JSON");
}

TEST_CASE("Store_leaves_newer_format_alone")
{
	TempDir dir;
	const std::string newer = R"({"format": 2, "data": {"from": "a later OpenComposite"}})";
	std::ofstream(dir.path / "settings.json") << newer;
	SharedStore store(dir.path);
	CHECK(store.Read(StoreDoc::Settings)["from"].asString() == "a later OpenComposite"); // read, as far as it's understood
	CHECK_FALSE(store.Update(StoreDoc::Settings, [](Json::Value& data) { data["from"] = "this one"; }));
	CHECK(Contents(dir.path / "settings.json") == newer);
}

TEST_CASE("Store_in_non_ascii_folder")
{
	TempDir dir(fs::path(u8"Jürgen_テスト"));
	SharedStore store(dir.path);
	CHECK(store.Update(StoreDoc::Overlays, [](Json::Value& data) { data["n"] = 1; }));
	CHECK(store.Read(StoreDoc::Overlays)["n"].asInt() == 1);
	CHECK(fs::exists(dir.path / "overlays.json"));
}

TEST_CASE("Store_events_reach_other_instance_not_self")
{
	TempDir dir;
	SharedStore a(dir.path), b(dir.path);
	uint64_t cursorA = 0, cursorB = 0;
	a.PollEvents(cursorA); // both start at the current head
	b.PollEvents(cursorB);
	a.PostEvent(700, { 1, 2, 3, 4 });

	std::vector<StoreEvent> got = b.PollEvents(cursorB);
	REQUIRE(got.size() == 1);
	CHECK(got[0].eventType == 700);
	CHECK(got[0].data[0] == 1);
	CHECK(got[0].data[3] == 4);
	CHECK(got[0].pid == oc_current_pid());
	CHECK(b.PollEvents(cursorB).empty()); // each event once

	CHECK(a.PollEvents(cursorA).empty()); // not its own
}

TEST_CASE("Store_event_ring_wraps")
{
	TempDir dir;
	SharedStore a(dir.path), b(dir.path);
	uint64_t cursor = 0;
	b.PollEvents(cursor);
	for (uint64_t i = 0; i < 300; i++)
		a.PostEvent(1000, { i, 0, 0, 0 });

	uint32_t lost = 0;
	std::vector<StoreEvent> got = b.PollEvents(cursor, &lost);
	CHECK(got.size() == 256);
	CHECK(lost == 44);
	REQUIRE(!got.empty());
	CHECK(got.front().data[0] == 44);
	CHECK(got.back().data[0] == 299);
}

TEST_CASE("Store_unusable_dir_fails_softly")
{
	TempDir dir;
	std::ofstream(dir.path / "a file") << "not a folder";
	SharedStore store(dir.path / "a file" / "store");
	CHECK_FALSE(store.Update(StoreDoc::Settings, [](Json::Value& data) { data["x"] = 1; }));
	CHECK(store.Read(StoreDoc::Settings).empty());
	CHECK(store.Generation(StoreDoc::Settings) == 0);
	store.PostEvent(1, { 0, 0, 0, 0 });
	uint64_t cursor = 0;
	CHECK(store.PollEvents(cursor).empty());
}

TEST_CASE("Store_process_alive")
{
	CHECK(SharedStore::IsProcessAlive(oc_current_pid()));
	CHECK_FALSE(SharedStore::IsProcessAlive(0));
}

TEST_CASE("Store_migrates_the_old_folders_documents_once")
{
	TempDir t;
	fs::path legacy = t.root / "OpenComposite" / "store";
	fs::path fresh = t.root / "OpenLamina" / "store";
	fs::create_directories(legacy);
	{
		SharedStore old(legacy); // the documents as a store writes them
		REQUIRE(old.Update(StoreDoc::Settings, [](Json::Value& data) { data["a"] = 1; }));
		REQUIRE(old.Update(StoreDoc::Chaperone, [](Json::Value& data) { data["room"] = 2; }));
	}
	REQUIRE(fs::exists(legacy / "state.bin"));
	REQUIRE(fs::exists(legacy / "store.lock"));

	REQUIRE(SharedStore::MigrateLegacyDir(legacy, fresh));
	CHECK(Contents(fresh / "settings.json") == Contents(legacy / "settings.json"));
	CHECK(Contents(fresh / "chaperone.json") == Contents(legacy / "chaperone.json"));
	CHECK_FALSE(fs::exists(fresh / "state.bin")); // running state isn't carried over
	CHECK_FALSE(fs::exists(fresh / "store.lock"));
	CHECK(fs::exists(legacy / "settings.json")); // a copy: the old folder is as it was
	CHECK(fs::exists(legacy / "state.bin"));

	// Once: a later start doesn't write the old documents over what the new folder has changed since
	std::ofstream(fresh / "settings.json", std::ios::trunc) << "changed";
	CHECK_FALSE(SharedStore::MigrateLegacyDir(legacy, fresh));
	CHECK(Contents(fresh / "settings.json") == "changed");

	// and what was copied is a store that reads it (settings is "changed" by now, so the other document)
	SharedStore store(fresh);
	CHECK(store.Read(StoreDoc::Chaperone)["room"].asInt() == 2);
}

TEST_CASE("Store_migration_leaves_nothing_behind_when_it_fails")
{
	TempDir t;
	fs::path legacy = t.root / "OpenComposite" / "store";
	fs::create_directories(legacy);
	std::ofstream(legacy / "settings.json") << "{}";
	std::ofstream(t.root / "OpenLamina") << "a file where its folder would go";
	CHECK_FALSE(SharedStore::MigrateLegacyDir(legacy, t.root / "OpenLamina" / "store"));
	CHECK(fs::exists(legacy / "settings.json"));
}
