// dllmain.cpp : Defines the entry point for the DLL application.
#include "stdafx.h"

#include "plat.h"

#include "Reimpl/Interfaces.h"

// Needed for the system-wide usage of this DLL (when renamed to vrclient[_x64].dll)
#include "generated/GVRClientCore.gen.h"

#include "Misc/Config.h"
#include "Misc/debug_helper.h"
#include "steamvr_abi.h"
#include <map>
#include <memory>

// Specific to OCOVR
#include "Drivers/Backend.h"
#include "Drivers/DriverManager.h"
#include "DrvOpenXR.h"

HMODULE openovr_module_id;
HMODULE chainedImplementation;

#if defined(_DEBUG)
#include <crtdbg.h>

// Debug builds, for the tests: with OC_CRT_LEAK_REPORT_DIR set, the debug C runtime writes what was never freed to
// <dir>/<process id>.txt when the DLL unloads, and its error and assertion reports there too
static void StartLeakReport()
{
	wchar_t dir[MAX_PATH];
	DWORD len = GetEnvironmentVariableW(L"OC_CRT_LEAK_REPORT_DIR", dir, MAX_PATH);
	if (len == 0 || len >= MAX_PATH - 16)
		return;
	std::wstring path = std::wstring(dir) + L"\\" + std::to_wstring(GetCurrentProcessId()) + L".txt";
	HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
	    FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return;
	_CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_WARN, file);
	_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_ERROR, file);
	_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_ASSERT, file);
	_CrtSetDbgFlag(_CrtSetDbgFlag(_CRTDBG_REPORT_FLAG) | _CRTDBG_LEAK_CHECK_DF);
}
#endif

BOOL APIENTRY DllMain(HMODULE hModule,
    DWORD ul_reason_for_call,
    LPVOID lpReserved)
{
	switch (ul_reason_for_call) {
	case DLL_PROCESS_ATTACH:
		openovr_module_id = hModule;
#if defined(_DEBUG)
		DbgSetModule(hModule);
		StartLeakReport();
#endif
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
		break;
	case DLL_PROCESS_DETACH:
		if (chainedImplementation) {
			FreeLibrary(chainedImplementation);
		}
		break;
	}
	return TRUE;
}

// Returns the last Win32 error, in string format. Returns an empty string if there is no error.
static string GetLastErrorAsString()
{
	// Get the error message, if any.
	DWORD errorMessageID = ::GetLastError();
	if (errorMessageID == 0)
		return "<no error>"; // No error message has been recorded

	LPSTR messageBuffer = nullptr;
	size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
	    NULL, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);

	string message(messageBuffer, size);

	// Free the buffer.
	LocalFree(messageBuffer);

	return message;
}

alternativeCoreFactory_t PlatformGetAlternativeCoreFactory()
{
	// nullptr (logged) if SteamVR can't be found or loaded: VRClientCoreFactory then uses OpenComposite
	string path = BaseClientCore::GetAlternativeRuntimePath();
	if (path.empty())
		return nullptr;

	string dll = path + "\\bin\\vrclient";
#if defined(_WIN64)
	dll += "_x64";
#endif
	dll += ".dll";

	// Make SteamVR load the rest of it's stuff from the correct place
	bool success = SetEnvironmentVariableA("VR_OVERRIDE", path.c_str());
	if (!success) {
		OOVR_LOGF("Failed to update SteamVR environment variable with error: %s", GetLastErrorAsString().c_str());
		return nullptr;
	}

	chainedImplementation = LoadLibraryA(dll.c_str());
	if (!chainedImplementation) {
		OOVR_LOGF("Failed to load SteamVR DLL '%s' with error: %s", dll.c_str(), GetLastErrorAsString().c_str());
		return nullptr;
	}

	auto factory = (alternativeCoreFactory_t)GetProcAddress(chainedImplementation, "VRClientCoreFactory");
	if (!factory)
		OOVR_LOGF("SteamVR DLL '%s' has no VRClientCoreFactory", dll.c_str());
	return factory;
}
