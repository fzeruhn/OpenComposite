#pragma once

// State that every OpenComposite instance of the user shares: JSON documents (settings, applications, the room
// setup, the scene app, the dashboard, overlays) and a ring of events between processes. Reading never blocks;
// changes are a locked read-modify-write, so two programs changing one document at once both keep their change.

#include "json/json.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

enum class StoreDoc { Settings, Applications, Chaperone, Scene, Dashboard, Overlays };

struct StoreEvent {
	uint64_t seq; // 1, 2, ... across every process; 0 is never an event's
	uint32_t pid; // who posted it
	uint32_t eventType;
	uint64_t data[4];
};
static_assert(sizeof(StoreEvent) == 48, "the ring's events are 48 bytes");

class SharedStore {
public:
	/** The process's store: OC_STORE_DIR if set, else %LOCALAPPDATA%\OpenComposite\store (Linux: $XDG_STATE_HOME or
	 * ~/.local/state, then OpenComposite/store) */
	static SharedStore& Get();

	/** A store in dir (tests). A folder that can't be made or used leaves the store unusable, never throwing. */
	explicit SharedStore(const std::filesystem::path& dir);
	~SharedStore();
	SharedStore(const SharedStore&) = delete;
	SharedStore& operator=(const SharedStore&) = delete;

	/** A copy of the document's data (an empty object if there's none, it doesn't parse, or the store can't be used),
	 * re-read from disk only when its generation changed */
	Json::Value Read(StoreDoc doc);

	/** Locked read-modify-write of the document's data. False if the store can't be used, or the file is a newer
	 * format (which is read but never written) */
	bool Update(StoreDoc doc, const std::function<void(Json::Value& data)>& edit);

	/** Changes with every Update of the document, in any process; 0 if the store can't be used */
	uint64_t Generation(StoreDoc doc);

	/** Puts an event in the ring for the other processes (and other stores in this one) */
	void PostEvent(uint32_t eventType, const std::array<uint64_t, 4>& data);

	/** The events other stores posted after cursor, which moves to the newest. Cursor 0 starts at the current head
	 * (nothing returned). More than the ring holds since cursor: the oldest are gone, and *lost says how many. */
	std::vector<StoreEvent> PollEvents(uint64_t& cursor, uint32_t* lost = nullptr);

	static bool IsProcessAlive(uint32_t pid);

	static constexpr uint32_t kFormat = 1;
	static constexpr uint32_t kRingSize = 256;

	// SharedStore.cpp's: the lock file and mapping, and state.bin's header
	struct Platform;
	struct Header;

private:

	bool Usable() const { return state != nullptr; }
	std::filesystem::path DocPath(StoreDoc doc) const;
	Header* header() const;
	StoreEvent* ring() const;

	std::filesystem::path dir;
	std::unique_ptr<Platform> platform;
	uint8_t* state = nullptr; // state.bin, mapped

	std::mutex writeLock; // this store's writers, which share its lock file handle
	std::mutex lock; // the cache and ownEvents
	struct Cached {
		uint64_t generation = 0;
		Json::Value data{ Json::objectValue };
		bool loaded = false;
	};
	std::map<StoreDoc, Cached> cache;
	std::set<uint64_t> ownEvents; // seqs this store posted, which its own polls skip
};
