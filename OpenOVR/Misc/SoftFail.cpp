#include "stdafx.h"

#include "Misc/SoftFail.h"

#include "Misc/Config.h"
#include "logging.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace {

// Never destroyed: a game's atexit handler may call VR_Shutdown, which logs the summary, after static destructors
std::mutex& SitesLock()
{
	static std::mutex& lock = *new std::mutex;
	return lock;
}

std::vector<const OcSoftSite*>& Sites()
{
	static std::vector<const OcSoftSite*>& sites = *new std::vector<const OcSoftSite*>;
	return sites;
}

std::atomic<void (*)(const char*)> abort_hook{ nullptr };
std::atomic<int> stop_override{ -1 };

const char* BaseName(const char* path)
{
	const char* name = path;
	for (const char* p = path; *p; p++)
		if (*p == '/' || *p == '\\')
			name = p + 1;
	return name;
}

} // namespace

void oc_soft_fail(OcSoftSite* site, const char* fmt, ...)
{
	// Only the first hit does anything but count
	if (site->hits.fetch_add(1) != 0)
		return;

	char buff[1024];
	va_list args;
	va_start(args, fmt);
	std::vsnprintf(buff, sizeof(buff), fmt, args);
	va_end(args);
	buff[sizeof(buff) - 1] = 0;

	{
		std::lock_guard<std::mutex> g(SitesLock());
		size_t n = strnlen(buff, sizeof(site->firstMessage) - 1);
		memcpy(site->firstMessage, buff, n);
		site->firstMessage[n] = 0;
		Sites().push_back(site);
	}
	oovr_log_raw_format(site->file, site->line, site->func, "[unsupported, logged once] %s", buff);

	int override = stop_override.load();
	bool stop = override >= 0 ? override == 1 : oovr_global_configuration.StopOnSoftAbort();
	if (!stop)
		return;
	if (auto hook = abort_hook.load()) {
		hook(buff);
		return;
	}
	oovr_abort_raw(site->file, site->line, site->func, "%s", "OpenComposite Debug Error (stopOnSoftAbort)", buff);
}

void oc_soft_log_summary()
{
	std::vector<const OcSoftSite*> sites = oc_soft_sites();
	uint64_t total = 0;
	for (const OcSoftSite* s : sites)
		total += s->hits.load();
	OOVR_LOGF("OpenComposite compatibility summary: %llu unsupported calls in %zu places", (unsigned long long)total, sites.size());
	for (const OcSoftSite* s : sites)
		OOVR_LOGF("  %u x %s (%s:%d): %s", s->hits.load(), s->func, BaseName(s->file), s->line, s->firstMessage);
}

std::vector<const OcSoftSite*> oc_soft_sites()
{
	std::lock_guard<std::mutex> g(SitesLock());
	return Sites();
}

void oc_soft_set_abort_hook(void (*hook)(const char* message))
{
	abort_hook.store(hook);
}

void oc_soft_set_stop_for_tests(int value)
{
	stop_override.store(value);
}
