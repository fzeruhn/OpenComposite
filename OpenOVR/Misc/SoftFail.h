#pragma once
// Soft failures: what OpenComposite does instead of ending the program when it can't do what was asked.
//
// Each use site is a function-local static OcSoftSite. Its first hit logs one line, every hit is counted, and
// oc_soft_log_summary() (called at shutdown) lists every site with its count, so a user's log says what the program
// needed that OpenComposite doesn't do. With stopOnSoftAbort=true in opencomposite.ini, the first hit of a site ends
// the program (for debugging).
//
//   OC_UNSUPPORTED(value, "fmt", ...)      log once, count, return value
//   OC_UNSUPPORTED_VOID("fmt", ...)
//   OC_SOFT_CHECK(expr, value, "fmt", ...) if expr is false: log once, count, return value
//   OC_SOFT_CHECK_VOID(expr, "fmt", ...)
//   OC_XR_SOFT(expr, value)                if the XrResult failed: log once, count, return value
//   OC_XR_SOFT_VOID(expr)
//   OC_FATAL("message")                    the only hard stop left; every use must be in scripts/fatal_sites.txt
//
// A value containing a top-level comma (a braced initialiser) must be put in parentheses.

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#ifndef OC_SOFT_PRINTF
#ifdef _WIN32
#define OC_SOFT_PRINTF(f, a)
#else
#define OC_SOFT_PRINTF(f, a) __attribute__((format(printf, f, a)))
#endif
#endif

struct OcSoftSite {
	const char* file;
	int line;
	const char* func;
	std::atomic<uint32_t> hits{ 0 };
	// Set once, at the first hit. Not a std::string: each site is a static, and the summary may be logged from an atexit
	// handler after static destructors have run
	char firstMessage[256] = {};

	OcSoftSite(const char* f, int l, const char* fn)
	    : file(f), line(l), func(fn)
	{
	}
};

// Logs on the first hit of the site, counts every hit; with stopOnSoftAbort the first hit calls the abort hook or ends
// the program
void oc_soft_fail(OcSoftSite* site, const char* fmt, ...) OC_SOFT_PRINTF(2, 3);

// "OpenComposite compatibility summary: <n> unsupported calls in <m> places", then one line per site
void oc_soft_log_summary();

// Every site hit so far, in the order of their first hit
std::vector<const OcSoftSite*> oc_soft_sites();

// Tests: called instead of ending the program when a stop is due (nullptr restores the default)
void oc_soft_set_abort_hook(void (*hook)(const char* message));
// Tests: 1 or 0 forces stopOnSoftAbort on or off, -1 goes back to the configuration
void oc_soft_set_stop_for_tests(int value);

#define OC_SOFT_SITE_ static OcSoftSite oc_soft_site_(__FILE__, __LINE__, __func__)

#define OC_UNSUPPORTED(ret, ...)                     \
	do {                                             \
		OC_SOFT_SITE_;                               \
		oc_soft_fail(&oc_soft_site_, __VA_ARGS__);   \
		return ret;                                  \
	} while (0)

#define OC_UNSUPPORTED_VOID(...)                     \
	do {                                             \
		OC_SOFT_SITE_;                               \
		oc_soft_fail(&oc_soft_site_, __VA_ARGS__);   \
		return;                                      \
	} while (0)

#define OC_SOFT_CHECK(expr, ret, ...)                    \
	do {                                                 \
		if (!(expr)) {                                   \
			OC_SOFT_SITE_;                               \
			oc_soft_fail(&oc_soft_site_, __VA_ARGS__);   \
			return ret;                                  \
		}                                                \
	} while (0)

#define OC_SOFT_CHECK_VOID(expr, ...)                    \
	do {                                                 \
		if (!(expr)) {                                   \
			OC_SOFT_SITE_;                               \
			oc_soft_fail(&oc_soft_site_, __VA_ARGS__);   \
			return;                                      \
		}                                                \
	} while (0)

#define OC_XR_SOFT(expr, ret)                                                                                 \
	do {                                                                                                      \
		XrResult oc_xr_soft_result_ = (expr);                                                                 \
		if (XR_FAILED(oc_xr_soft_result_)) {                                                                  \
			OC_SOFT_SITE_;                                                                                    \
			oc_soft_fail(&oc_soft_site_, "OpenXR call failed (%d): %s", (int)oc_xr_soft_result_, #expr);   \
			return ret;                                                                                       \
		}                                                                                                     \
	} while (0)

#define OC_XR_SOFT_VOID(expr)                                                                                 \
	do {                                                                                                      \
		XrResult oc_xr_soft_result_ = (expr);                                                                 \
		if (XR_FAILED(oc_xr_soft_result_)) {                                                                  \
			OC_SOFT_SITE_;                                                                                    \
			oc_soft_fail(&oc_soft_site_, "OpenXR call failed (%d): %s", (int)oc_xr_soft_result_, #expr);   \
			return;                                                                                           \
		}                                                                                                     \
	} while (0)

#define OC_FATAL(msg) OOVR_ABORT(msg)
