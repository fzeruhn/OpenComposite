//
// Created by ZNix on 8/02/2021.
//

#include "stdafx.h"

#include "Misc/debugbreak.h"
#include "resources.h"

#include <stdlib.h>

#ifndef _WIN32

void ZeroMemory(void* data, size_t len)
{
	memset(data, 0, len);
}

// MSVC's would end the program on a string that doesn't fit; these cut it short and always terminate (if there's room)
void strncpy_s(char* dest, size_t dest_size, char const* src, size_t max)
{
	if (!dest || dest_size == 0)
		return;
	if (!src) {
		dest[0] = 0;
		return;
	}
	size_t n = strnlen(src, max);
	if (n >= dest_size)
		n = dest_size - 1;
	memcpy(dest, src, n);
	dest[n] = 0;
}

void strcpy_s(char* dest, size_t dest_size, char const* src)
{
	strncpy_s(dest, dest_size, src, dest_size);
}

void DebugBreak()
{
	// This will cause a crash if we're not being debugged, so require an environment variable to be set and non-empty.
	const char* debug_break_env = getenv("OPENCOMPOSITE_DEBUG_BREAK");
	if (debug_break_env && debug_break_env[0] != 0)
		debug_break();
}

// Our hacky resource lookup implementation - a symbol is defined for the start and end of each file. We then have a case
// that grabs the correct symbols, so it's roughly-ish usable in place of the Windows resource system.
#define DEF_RESOURCE(name)                   \
	extern "C" const char resource_##name[]; \
	extern "C" const char resource_##name##_end[];

RES_LIST_LINUX(DEF_RESOURCE)
#undef DEF_RESOURCE

void FindResourceLinux(int id, const char** start, const char** end)
{
	switch (id) {
#define RESOURCE_CASE(name)           \
	case name:                        \
		*start = resource_##name;     \
		*end = resource_##name##_end; \
		break;

		RES_LIST_LINUX(RESOURCE_CASE);
#undef RESOURCE_CASE
	default:
		// The callers check for an empty resource
		OOVR_LOGF("Invalid resource ID %d", id);
		*start = nullptr;
		*end = nullptr;
	}
}

#endif
