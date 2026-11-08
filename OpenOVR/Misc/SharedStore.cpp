#include "stdafx.h"

#include "SharedStore.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

constexpr size_t kStateSize = 16 * 1024;
constexpr size_t kHeaderSize = 128;
constexpr const char* kDocNames[] = { "settings", "applications", "chaperone", "scene", "dashboard", "overlays" };

uint64_t Load(uint64_t& v)
{
	return std::atomic_ref<uint64_t>(v).load(std::memory_order_acquire);
}

void Store(uint64_t& v, uint64_t value)
{
	std::atomic_ref<uint64_t>(v).store(value, std::memory_order_release);
}

// How long a read or replace is tried again: well under the 2 s another program waits for the lock (Windows' sleeps
// round up to its 15.6 ms timer, so this is a time, not a count)
constexpr auto kRetryFor = std::chrono::milliseconds(500);

enum class ReadResult { Ok, Missing, Failed };

// The whole file, opened so that another process may replace it meanwhile. Missing if there's none; Failed if it's there
// but can't be read now (another program's antivirus or backup holding it, say), after trying again up to attempts
// times, 5 ms apart, and for kRetryFor at most.
ReadResult ReadShared(const fs::path& path, std::string& out, int attempts)
{
	auto deadline = std::chrono::steady_clock::now() + kRetryFor;
	for (int attempt = 0;; attempt++) {
		out.clear();
#ifdef _WIN32
		HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
		    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (h == INVALID_HANDLE_VALUE) {
			DWORD err = GetLastError();
			if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
				return ReadResult::Missing;
		} else {
			char buffer[16384];
			DWORD got = 0;
			bool ok = true;
			while (true) {
				if (!ReadFile(h, buffer, sizeof(buffer), &got, nullptr)) {
					ok = false;
					break;
				}
				if (got == 0)
					break;
				out.append(buffer, got);
			}
			CloseHandle(h);
			if (ok)
				return ReadResult::Ok;
		}
#else
		int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
		if (fd < 0) {
			if (errno == ENOENT || errno == ENOTDIR)
				return ReadResult::Missing;
		} else {
			char buffer[16384];
			ssize_t got;
			while ((got = read(fd, buffer, sizeof(buffer))) > 0 || (got < 0 && errno == EINTR)) {
				if (got > 0)
					out.append(buffer, (size_t)got);
			}
			close(fd);
			if (got == 0)
				return ReadResult::Ok;
		}
#endif
		if (attempt + 1 >= attempts || std::chrono::steady_clock::now() >= deadline)
			return ReadResult::Failed;
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
}

// The document's root object, or false if it doesn't parse (jsoncpp throws past its nesting limit)
bool ParseDoc(const std::string& text, Json::Value& root)
{
	Json::CharReaderBuilder builder;
	std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
	std::string error;
	try {
		return reader->parse(text.data(), text.data() + text.size(), &root, &error) && root.isObject();
	} catch (const std::exception&) {
		return false;
	}
}

// Moves from over to, replacing it. On Windows that fails (access denied, or a sharing violation) while another handle
// has the file open: a reader in some program, an antivirus or an indexer. None holds it long: tried again for
// kRetryFor.
bool Replace(const fs::path& from, const fs::path& to)
{
#ifdef _WIN32
	auto deadline = std::chrono::steady_clock::now() + kRetryFor;
	for (;;) {
		if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING))
			return true;
		DWORD err = GetLastError();
		if ((err != ERROR_ACCESS_DENIED && err != ERROR_SHARING_VIOLATION) || std::chrono::steady_clock::now() >= deadline)
			return false;
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
#else
	return rename(from.c_str(), to.c_str()) == 0;
#endif
}

bool WriteFile(const fs::path& path, const std::string& text)
{
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out)
		return false;
	out << text;
	out.close();
	return !out.fail();
}

fs::path DefaultDir()
{
#ifdef _WIN32
	if (const wchar_t* overrideDir = _wgetenv(L"OC_STORE_DIR"); overrideDir && overrideDir[0])
		return fs::path(overrideDir);
	if (const wchar_t* local = _wgetenv(L"LOCALAPPDATA"); local && local[0])
		return fs::path(local) / "OpenComposite" / "store";
	return {};
#else
	if (const char* overrideDir = getenv("OC_STORE_DIR"); overrideDir && overrideDir[0])
		return fs::path(overrideDir);
	const char* xdg = getenv("XDG_STATE_HOME");
	if (xdg && xdg[0])
		return fs::path(xdg) / "OpenComposite" / "store";
	const char* home = getenv("HOME");
	if (home && home[0])
		return fs::path(home) / ".local" / "state" / "OpenComposite" / "store";
	return {};
#endif
}

} // namespace

struct SharedStore::Header {
	char magic[4]; // "OCST"
	uint32_t format;
	uint64_t generations[8]; // one per StoreDoc
	uint64_t head; // the newest event's seq (0: none yet)
	uint64_t reserved[6];
};
static_assert(sizeof(SharedStore::Header) <= kHeaderSize, "the header fits before the ring");
static_assert(kHeaderSize + SharedStore::kRingSize * sizeof(StoreEvent) <= kStateSize, "the ring fits in state.bin");

// The lock file and the mapping of state.bin
struct SharedStore::Platform {
#ifdef _WIN32
	HANDLE lockFile = INVALID_HANDLE_VALUE;
	HANDLE stateFile = INVALID_HANDLE_VALUE;
	HANDLE mapping = nullptr;
#else
	int lockFd = -1;
	int stateFd = -1;
#endif
	void* view = nullptr;

	bool OpenLock(const fs::path& path)
	{
#ifdef _WIN32
		lockFile = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		    nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		return lockFile != INVALID_HANDLE_VALUE;
#else
		lockFd = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
		return lockFd >= 0;
#endif
	}

	// Waits for about two seconds at most: a holder that is suspended (in a debugger, say) leaves the store unwritten
	// rather than every other program stuck
	bool Lock()
	{
		auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		for (;;) {
#ifdef _WIN32
			OVERLAPPED overlapped = {};
			if (LockFileEx(lockFile, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &overlapped))
				return true;
			if (GetLastError() != ERROR_LOCK_VIOLATION)
				return false;
#else
			if (flock(lockFd, LOCK_EX | LOCK_NB) == 0)
				return true;
			if (errno != EWOULDBLOCK && errno != EINTR)
				return false;
#endif
			if (std::chrono::steady_clock::now() >= deadline)
				return false;
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
	}

	void Unlock()
	{
#ifdef _WIN32
		OVERLAPPED overlapped = {};
		UnlockFileEx(lockFile, 0, 1, 0, &overlapped);
#else
		flock(lockFd, LOCK_UN);
#endif
	}

	// state.bin, made kStateSize long if it's shorter, mapped (two programs doing this at once both get the same file)
	bool MapState(const fs::path& path)
	{
#ifdef _WIN32
		stateFile = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		    nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (stateFile == INVALID_HANDLE_VALUE)
			return false;
		// A writable mapping larger than the file extends it
		mapping = CreateFileMappingW(stateFile, nullptr, PAGE_READWRITE, 0, (DWORD)kStateSize, nullptr);
		if (!mapping)
			return false;
		view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, kStateSize);
		return view != nullptr;
#else
		stateFd = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
		if (stateFd < 0)
			return false;
		struct stat st = {};
		if (fstat(stateFd, &st) != 0)
			return false;
		// Allocated, not sparse: a full disk fails here rather than with SIGBUS on a write to the mapping
		if ((size_t)st.st_size < kStateSize && posix_fallocate(stateFd, 0, kStateSize) != 0)
			return false;
		view = mmap(nullptr, kStateSize, PROT_READ | PROT_WRITE, MAP_SHARED, stateFd, 0);
		if (view == MAP_FAILED) {
			view = nullptr;
			return false;
		}
		return true;
#endif
	}

	~Platform()
	{
#ifdef _WIN32
		if (view)
			UnmapViewOfFile(view);
		if (mapping)
			CloseHandle(mapping);
		if (stateFile != INVALID_HANDLE_VALUE)
			CloseHandle(stateFile);
		if (lockFile != INVALID_HANDLE_VALUE)
			CloseHandle(lockFile);
#else
		if (view)
			munmap(view, kStateSize);
		if (stateFd >= 0)
			close(stateFd);
		if (lockFd >= 0)
			close(lockFd);
#endif
	}
};

// Holds the store's file lock for a scope
class StoreFileLock {
public:
	explicit StoreFileLock(SharedStore::Platform& p)
	    : platform(p), held(p.Lock())
	{
	}
	~StoreFileLock()
	{
		if (held)
			platform.Unlock();
	}

private:
	SharedStore::Platform& platform;

public:
	const bool held;
};

SharedStore& SharedStore::Get()
{
	// Never destroyed: the backend writes to it from static destructors at exit, which may run after this one would
	static SharedStore* store = new SharedStore(DefaultDir());
	return *store;
}

SharedStore::SharedStore(const fs::path& directory)
    : dir(directory), platform(std::make_unique<Platform>())
{
	std::error_code ec;
	if (dir.empty())
		return;
	fs::create_directories(dir, ec);
	if (!fs::is_directory(dir, ec))
		return;
	auto unusable = [&](const char* why) {
		std::u8string name = dir.u8string();
		OOVR_LOGF("Shared store in %s not usable: %s", std::string(name.begin(), name.end()).c_str(), why);
	};
	if (!platform->OpenLock(dir / "store.lock") || !platform->MapState(dir / "state.bin")) {
		unusable("its files can't be opened");
		return;
	}

	uint8_t* view = (uint8_t*)platform->view;
	Header* h = (Header*)view;
	auto current = [&] { return memcmp(h->magic, "OCST", 4) == 0 && h->format == kFormat; };
	// The lock only to set up a new or older state.bin: another program's long write doesn't leave this one without a store
	if (!current()) {
		StoreFileLock fileLock(*platform);
		if (!fileLock.held) {
			unusable("another program holds its lock");
			return;
		}
		if (memcmp(h->magic, "OCST", 4) == 0 && h->format > kFormat) {
			unusable("it's a later OpenComposite's");
			return;
		}
		if (!current()) {
			memset(view, 0, kStateSize);
			h->format = kFormat;
			memcpy(h->magic, "OCST", 4);
		}
	}
	state = view;
}

SharedStore::~SharedStore() = default;

SharedStore::Header* SharedStore::header() const
{
	return (Header*)state;
}

StoreEvent* SharedStore::ring() const
{
	return (StoreEvent*)(state + kHeaderSize);
}

fs::path SharedStore::DocPath(StoreDoc doc) const
{
	return dir / (std::string(kDocNames[(int)doc]) + ".json");
}

uint64_t SharedStore::Generation(StoreDoc doc)
{
	return Usable() ? Load(header()->generations[(int)doc]) : 0;
}

Json::Value SharedStore::Read(StoreDoc doc)
{
	if (!Usable())
		return Json::Value(Json::objectValue);
	uint64_t generation;
	{
		std::lock_guard<std::mutex> guard(lock);
		generation = Load(header()->generations[(int)doc]);
		Cached& cached = cache[doc];
		if (cached.loaded && cached.generation == generation)
			return cached.data;
	}

	Json::Value data(Json::objectValue);
	std::string text;
	ReadResult result = ReadShared(DocPath(doc), text, 4);
	Json::Value root;
	// A document that doesn't parse reads as empty; one of a later format is read as far as it's understood
	if (result == ReadResult::Ok && ParseDoc(text, root) && root["data"].isObject())
		data = root["data"];

	std::lock_guard<std::mutex> guard(lock);
	Cached& cached = cache[doc];
	if (result == ReadResult::Failed)
		return cached.data; // what was read last; read again next time
	if (!cached.loaded || cached.generation <= generation)
		cached = { generation, data, true };
	return data;
}

bool SharedStore::Update(StoreDoc doc, const std::function<void(Json::Value& data)>& edit)
{
	if (!Usable())
		return false;
	// Not the cache's lock: Read and PollEvents mustn't wait while this waits for another process
	std::lock_guard<std::mutex> writing(writeLock);
	StoreFileLock fileLock(*platform);
	if (!fileLock.held)
		return false;

	fs::path path = DocPath(doc);
	Json::Value data(Json::objectValue);
	std::string text;
	switch (ReadShared(path, text, 200)) {
	case ReadResult::Failed:
		return false; // there but unreadable: writing now would lose what's in it
	case ReadResult::Missing:
		break;
	case ReadResult::Ok: {
		Json::Value root;
		if (ParseDoc(text, root)) {
			if (root["format"].isUInt() && root["format"].asUInt() > kFormat)
				return false; // a later OpenComposite's: read, never written
			if (root["data"].isObject())
				data = root["data"];
		} else {
			// Kept for a look, and replaced below
			fs::path bad = path;
			bad += ".bad";
			Replace(path, bad);
		}
		break;
	}
	}

	try {
		edit(data);
	} catch (const std::exception&) {
		return false; // a type error in the edit (jsoncpp throws): nothing written
	}
	if (!data.isObject())
		return false;

	Json::Value root(Json::objectValue);
	root["format"] = kFormat;
	root["data"] = data;
	Json::StreamWriterBuilder writer;
	writer["indentation"] = "\t";
	fs::path tmp = path;
	tmp += ".tmp";
	if (!WriteFile(tmp, Json::writeString(writer, root)) || !Replace(tmp, path))
		return false;

	uint64_t generation = Load(header()->generations[(int)doc]) + 1;
	Store(header()->generations[(int)doc], generation);
	std::lock_guard<std::mutex> guard(lock);
	cache[doc] = { generation, data, true };
	return true;
}

void SharedStore::PostEvent(uint32_t eventType, const std::array<uint64_t, 4>& data)
{
	if (!Usable())
		return;
	std::lock_guard<std::mutex> writing(writeLock);
	StoreFileLock fileLock(*platform);
	if (!fileLock.held)
		return;

	uint64_t seq = Load(header()->head) + 1;
	{
		// Marked as ours before it's visible, so a poll in this process never takes it for another's
		std::lock_guard<std::mutex> guard(lock);
		ownEvents.insert(seq);
		while (!ownEvents.empty() && *ownEvents.begin() + kRingSize < seq)
			ownEvents.erase(ownEvents.begin());
	}
	StoreEvent& slot = ring()[(size_t)((seq - 1) % kRingSize)];
	// seq 0 while it's written, so a reader never takes half an event for a whole one
	Store(slot.seq, 0);
	slot.pid = (uint32_t)
#ifdef _WIN32
	    GetCurrentProcessId();
#else
	    getpid();
#endif
	slot.eventType = eventType;
	for (int i = 0; i < 4; i++)
		slot.data[i] = data[i];
	Store(slot.seq, seq);
	Store(header()->head, seq);
}

std::vector<StoreEvent> SharedStore::PollEvents(uint64_t& cursor, uint32_t* lost)
{
	if (lost)
		*lost = 0;
	std::vector<StoreEvent> events;
	if (!Usable())
		return events;
	std::lock_guard<std::mutex> guard(lock);

	// cursor is the next seq to take
	uint64_t head = Load(header()->head);
	if (cursor == 0 || cursor > head + 1) {
		cursor = head + 1;
		return events;
	}
	uint64_t first = cursor;
	uint32_t missed = 0;
	if (head + 1 - first > kRingSize) {
		missed = (uint32_t)(head + 1 - first - kRingSize);
		first = head + 1 - kRingSize;
	}
	for (uint64_t seq = first; seq <= head; seq++) {
		StoreEvent& slot = ring()[(size_t)((seq - 1) % kRingSize)];
		if (Load(slot.seq) != seq) {
			missed++; // overwritten (or being written) since head was read
			continue;
		}
		StoreEvent copy;
		copy.pid = slot.pid;
		copy.eventType = slot.eventType;
		for (int i = 0; i < 4; i++)
			copy.data[i] = slot.data[i];
		std::atomic_thread_fence(std::memory_order_acquire);
		if (Load(slot.seq) != seq) {
			missed++;
			continue;
		}
		copy.seq = seq;
		if (!ownEvents.count(seq))
			events.push_back(copy);
	}
	cursor = head + 1;
	if (lost)
		*lost = missed;
	return events;
}

bool SharedStore::IsProcessAlive(uint32_t pid)
{
	if (pid == 0)
		return false;
#ifdef _WIN32
	HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
	if (!process)
		return GetLastError() == ERROR_ACCESS_DENIED; // there, but not ours to open
	bool alive = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
	CloseHandle(process);
	return alive;
#else
	return kill((pid_t)pid, 0) == 0 || errno == EPERM;
#endif
}
