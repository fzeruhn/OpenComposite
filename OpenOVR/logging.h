#pragma once

#include <atomic>
#include <string>

#include "Misc/SoftFail.h"

// Not strictly a logging thing, but makes clion happy about calling OOVR_ABORT and not returning
#ifdef _WIN32
#define OC_NORETURN __declspec(noreturn)
#define OC_PRINTF(format_pos, args_pos)
#else
#define OC_NORETURN __attribute__((noreturn))
#define OC_PRINTF(format_pos, args_pos) __attribute__((format(printf, format_pos, args_pos)))
#endif

// Signal-safe and thread-safe write
void oovr_printf_safe(const char* format, ...);
// Signal-safe and thread-safe flush
void oovr_flush_safe();

void oovr_log_raw(const char* file, long line, const char* func, const char* msg);
void oovr_log_raw_format(const char* file, long line, const char* func, const char* msg, ...) OC_PRINTF(4, 5);
#define OOVR_LOG(msg) oovr_log_raw(__FILE__, __LINE__, __FUNCTION__, msg)
#define OOVR_LOGF(...) oovr_log_raw_format(__FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)

// Tests only: every log line is also passed to sink (nullptr to stop)
void oovr_log_set_test_sink(void (*sink)(const char* line));

OC_NORETURN void oovr_abort_raw(const char* file, long line, const char* func, const char* msg, const char* title = nullptr, ...) OC_PRINTF(4, 6);

std::string GetEnv(const std::string& var);

#define OOVR_ABORT(msg)                                        \
	do {                                                       \
		oovr_abort_raw(__FILE__, __LINE__, __FUNCTION__, msg); \
	} while (0)
#define OOVR_ABORT_T(msg, title)                                      \
	do {                                                              \
		oovr_abort_raw(__FILE__, __LINE__, __FUNCTION__, msg, title); \
	} while (0)
#define OOVR_ABORTF(msg, ...)                                                        \
	do {                                                                             \
		oovr_abort_raw(__FILE__, __LINE__, __FUNCTION__, msg, nullptr, __VA_ARGS__); \
	} while (0)

/**
 * Perform a 'soft abort'. If OpenComposite is in debug mode this will cause a normal (hard) abort, but
 * otherwise will log a message once into the log and continue.
 *
 * This is designed to be used in a place where the game has requested some action that's not yet supported
 * and OpenComposite wants to mark it for developer attention while investigating the game, but at the same
 * time the game will probably continue working in a degraded state without this (for example, missing
 * haptics).
 *
 * Previously hard aborts were used for this, and unsurprisingly it caused a lot of trouble where what
 * should have been minor issues were making games crash at startup.
 */
#define OOVR_SOFT_ABORTF(msg, ...)                                           \
	do {                                                                     \
		static OcSoftSite oovr_soft_site_(__FILE__, __LINE__, __func__);     \
		oc_soft_fail(&oovr_soft_site_, msg, __VA_ARGS__);                    \
	} while (0)

// Work on GCC since it needs some varargs, and the actual value doesn't matter
#define OOVR_SOFT_ABORT(msg) OOVR_SOFT_ABORTF("%s", msg)

// Log once function - useful for warning that a function called many times isn't implemented, while using
// a workaround to make something mostly work.
#define OOVR_LOG_ONCE(msg) OOVR_LOG_ONCEF("%s", msg)
#define OOVR_LOG_ONCEF(msg, ...)                                                                   \
	do {                                                                                           \
		/* We don't need the function suffix, but it makes it easier to recognise in a debugger */ \
		static bool oovr_hit_log_once_##__FUNCTION__ = false;                                      \
		if (!oovr_hit_log_once_##__FUNCTION__) {                                                   \
			oovr_hit_log_once_##__FUNCTION__ = true;                                               \
			OOVR_LOGF("[once] " msg, __VA_ARGS__);                                                 \
		}                                                                                          \
	} while (false)

void oovr_message_raw(const char* message, const char* title);
#define OOVR_MESSAGE(message, title) oovr_message_raw(message, title);

// DirectX API validation helpers
#define OOVR_FAILED_DX_ABORT(expression)                                                                                       \
	do {                                                                                                                       \
		HRESULT res = (expression);                                                                                            \
		if (FAILED(res)) {                                                                                                     \
			OOVR_LOGF("DX Call failed with: 0x%08x", res);                                                                     \
			OOVR_ABORT_T("OOVR_FAILED_DX_ABORT failed on: " #expression, "OpenLamina DirectX error - see log for details"); \
		}                                                                                                                      \
	} while (0)

#define OOVR_FAILED_VK_ABORT(expression)                                              \
	do {                                                                              \
		VkResult failed_vk_abort_result = (expression);                               \
		if (failed_vk_abort_result < VK_SUCCESS) {                                    \
			OOVR_ABORTF("Vulkan Call failed, aborting. %s:%d %s. Error code: %d\n%s", \
			    __FILE__, __LINE__, __func__, failed_vk_abort_result, #expression);   \
		}                                                                             \
	} while (0)

// General validation helpers
#define OOVR_FALSE_ABORT(expression)                                      \
	do {                                                                  \
		if (!(expression)) {                                              \
			OOVR_ABORT("Expression is false unexpectedly: " #expression); \
		}                                                                 \
	} while (0)

// OVR API validation helpers
#define OOVR_FAILED_OVR_LOG(expression)                                                    \
	if (!OVR_SUCCESS(expression)) {                                                        \
		ovrErrorInfo e = {};                                                               \
		ovr_GetLastErrorInfo(&e);                                                          \
		OOVR_LOGF("OVR Call failed.  Error code: %d  Descr: %s", e.Result, e.ErrorString); \
	}

// TODO does OpenXR have an equivalent of ovr_GetLastErrorInfo?
#define OOVR_FAILED_XR_ABORT(expression)                                                  \
	do {                                                                                  \
		XrResult failed_xr_abort_result = (expression);                                   \
		if (XR_FAILED(failed_xr_abort_result)) {                                          \
			if (xr_instance != XR_NULL_HANDLE) {                                          \
				char buf[XR_MAX_RESULT_STRING_SIZE];                                      \
				xrResultToString(xr_instance, failed_xr_abort_result, buf);               \
				OOVR_ABORTF("OpenXR Call failed, aborting. %s:%d %s. Error code: %s\n%s", \
				    __FILE__, __LINE__, __func__, buf, #expression);                      \
			} else {                                                                      \
				OOVR_ABORTF("OpenXR Call failed, aborting. %s:%d %s. Error code: %d\n%s", \
				    __FILE__, __LINE__, __func__, failed_xr_abort_result, #expression);   \
			}                                                                             \
		}                                                                                 \
	} while (0)

#define OOVR_FAILED_XR_SOFT_ABORT(expression)                                       \
	do {                                                                            \
		XrResult failed_xr_abort_result = (expression);                             \
		if (XR_FAILED(failed_xr_abort_result)) {                                    \
			OOVR_SOFT_ABORTF("OpenXR Call failed. %s:%d %s. Error code: %d\n%s",    \
			    __FILE__, __LINE__, __func__, failed_xr_abort_result, #expression); \
		}                                                                           \
	} while (0)

// Again unrelated and just putting it here because everything can use it: copies src into the array dest, cutting it
// short (at a UTF-8 character boundary, and logging that once per call site) when it doesn't fit, where MSVC's
// strcpy_s would end the program. logged is the call site's flag.
void oc_strcpy_cut(char* dest, size_t dest_size, const char* src, std::atomic<bool>* logged, const char* file, long line);
#define strcpy_arr(dest, src) \
	oc_strcpy_cut(dest, sizeof(dest), src, [] { static std::atomic<bool> logged{ false }; return &logged; }(), __FILE__, __LINE__)

#define STUBBED()                                                                                                            \
	do {                                                                                                                     \
		std::string str = "Hit stubbed file at " __FILE__ ":" + std::to_string(__LINE__) + " func " + std::string(__func__); \
		OOVR_ABORT(str.c_str());                                                                                             \
	} while (0)
