// doctest first: stdafx.h brings in Xlib on Linux, whose macros (None, Bool, ...) clash with doctest's names
#include "doctest.h"

#include "stdafx.h"

#include "Misc/xrutil.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <optional>
#include <thread>

namespace {
using namespace std::chrono_literals;

// Polls until pred holds or the timeout passes; returns whether it held
bool WaitFor(const std::function<bool()>& pred, std::chrono::milliseconds timeout = 5s)
{
	auto end = std::chrono::steady_clock::now() + timeout;
	while (!pred()) {
		if (std::chrono::steady_clock::now() > end)
			return false;
		std::this_thread::sleep_for(1ms);
	}
	return true;
}

// Whether a flag stays unset for the whole of a short wait
bool StaysUnset(const std::atomic<bool>& flag)
{
	return !WaitFor([&] { return flag.load(); }, 150ms);
}

// A thread that takes the lock (exclusive or shared) and sets got once it holds it
struct Contender {
	std::atomic<bool> got{ false };
	std::thread thread;

	Contender(SessionWrapper& w, bool exclusive)
	    : thread([&w, exclusive, this] {
		    SessionLock l(w, exclusive);
		    got = true;
	    })
	{
	}
	~Contender() { thread.join(); }
};
} // namespace

TEST_CASE("SessionLock: an exclusive request under a shared hold waits for the other threads' shared holds")
{
	SessionWrapper w;
	std::atomic<bool> shared{ false }, exclusive{ false }, release{ false };

	std::optional<SessionLock> other;
	other.emplace(w, false);

	std::thread upgrader([&] {
		SessionLock outer = w.lock_shared();
		shared = true;
		{
			SessionLock inner = w.lock();
			exclusive = true;
			WaitFor([&] { return release.load(); });
		}
	});

	REQUIRE(WaitFor([&] { return shared.load(); }));
	CHECK(StaysUnset(exclusive));

	other.reset();
	CHECK(WaitFor([&] { return exclusive.load(); }));

	// While the upgraded lock is held, another thread's shared request waits too
	{
		Contender reader(w, false);
		CHECK(StaysUnset(reader.got));
		release = true;
	}
	upgrader.join();
}

TEST_CASE("SessionLock: nested requests of the same or a weaker kind change nothing")
{
	SessionWrapper w;

	SUBCASE("shared in shared")
	{
		std::optional<SessionLock> outer;
		outer.emplace(w, false);
		{
			SessionLock inner = w.lock_shared();
		}

		// Still held after the inner one ends: another thread's exclusive request waits for the outer one
		Contender writer(w, true);
		CHECK(StaysUnset(writer.got));
		outer.reset();
		CHECK(WaitFor([&] { return writer.got.load(); }));
	}

	SUBCASE("shared and exclusive in exclusive")
	{
		std::optional<SessionLock> outer;
		outer.emplace(w, true);
		{
			SessionLock inner = w.lock_shared();
			SessionLock again = w.lock();
		}

		// Still exclusive after the inner ones end: another thread's shared request waits for the outer one
		Contender reader(w, false);
		CHECK(StaysUnset(reader.got));
		outer.reset();
		CHECK(WaitFor([&] { return reader.got.load(); }));
	}
}

TEST_CASE("SessionLock: two threads upgrading at once both get through")
{
	SessionWrapper w;
	std::atomic<int> sharing{ 0 }, done{ 0 };

	auto upgrade = [&] {
		SessionLock outer = w.lock_shared();
		sharing++;
		// Both hold the shared lock before either asks for the exclusive one
		WaitFor([&] { return sharing.load() == 2; });
		{
			SessionLock inner = w.lock();
		}
		done++;
	};
	std::thread a(upgrade), b(upgrade);

	CHECK(WaitFor([&] { return done.load() == 2; }));
	a.join();
	b.join();
}

TEST_CASE("SessionLock: the shared hold is taken back after the exclusive request under it ends")
{
	SessionWrapper w;

	std::optional<SessionLock> outer;
	outer.emplace(w, false);
	{
		SessionLock inner = w.lock();
	}

	// Shared again: another thread's shared request gets through, an exclusive one waits for the outer hold
	{
		Contender reader(w, false);
		CHECK(WaitFor([&] { return reader.got.load(); }));
	}
	{
		Contender writer(w, true);
		CHECK(StaysUnset(writer.got));
		outer.reset();
		CHECK(WaitFor([&] { return writer.got.load(); }));
	}

	// This thread holds nothing now: a lock taken here is a real one again
	outer.emplace(w, true);
	{
		Contender reader(w, false);
		CHECK(StaysUnset(reader.got));
		outer.reset();
		CHECK(WaitFor([&] { return reader.got.load(); }));
	}
}

TEST_CASE("SessionLock: the teardown lock gives up on a holder that never lets go, and takes a free lock")
{
	SessionWrapper w;
	std::atomic<bool> held{ false }, release{ false };
	// A thread holding the shared lock without end, as one ended mid-frame at process exit would
	std::thread holder([&] {
		SessionLock l = w.lock_shared();
		held = true;
		WaitFor([&] { return release.load(); });
	});
	if (!WaitFor([&] { return held.load(); })) {
		release = true;
		holder.join();
		FAIL("the holder never took the lock");
	}
	{
		auto start = std::chrono::steady_clock::now();
		SessionLock teardown = w.lock_for_teardown(50ms);
		CHECK(std::chrono::steady_clock::now() - start >= 50ms);
		// Not held: another thread's shared request still gets through
		Contender reader(w, false);
		CHECK(WaitFor([&] { return reader.got.load(); }));
	}
	release = true;
	holder.join();

	// Free: it's the exclusive lock
	std::optional<SessionLock> teardown;
	teardown.emplace(w, std::chrono::milliseconds(1000));
	{
		Contender reader(w, false);
		CHECK(StaysUnset(reader.got));
		teardown.reset();
		CHECK(WaitFor([&] { return reader.got.load(); }));
	}
}

TEST_CASE("SessionLock_shared_reader_blocks_restart")
{
	// A reader of input on one thread (shared) and a session restart on another (exclusive): the restart waits
	SessionWrapper w;
	std::optional<SessionLock> reader;
	reader.emplace(w, false);

	Contender restart(w, true);
	CHECK(StaysUnset(restart.got));
	reader.reset();
	CHECK(WaitFor([&] { return restart.got.load(); }));
}
