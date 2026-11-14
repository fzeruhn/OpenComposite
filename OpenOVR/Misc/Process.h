#pragma once

#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

// This program's process id, as OpenVR's *ProcessId calls give it
inline uint32_t oc_current_pid()
{
#ifdef _WIN32
	return (uint32_t)GetCurrentProcessId();
#else
	return (uint32_t)getpid();
#endif
}
