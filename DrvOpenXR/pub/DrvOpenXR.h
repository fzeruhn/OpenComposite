#pragma once
#include "../OpenOVR/Drivers/Backend.h"

// Since we're not importing stdafx, add another nasty little hack
#ifndef _WIN32
#include "../OpenOVR/linux_funcs.h"
#endif

namespace DrvOpenXR {
// nullptr with *error set if OpenXR can't be started (108 HmdNotFound for no runtime or no headset, else 124
// Internal), with everything it had created destroyed again
IBackend* CreateOpenXRBackend(const char* startupInfo, vr::EVRInitError* error);
void GetXRAppName(char (&appName)[128]);
}; // namespace DrvOpenXR
