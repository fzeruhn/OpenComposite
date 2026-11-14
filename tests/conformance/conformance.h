#pragma once
// Shared between oc_conformance.cpp and cases.cpp.

#include "callers_support.h"

#include <cstdio>
#include <functional>
#include <map>
#include <string>

// The client core interface every vrclient DLL exports through VRClientCoreFactory (custom_interfaces/IVRClientCore_003.h)
namespace conformance {

class IVRClientCore {
public:
	virtual vr::EVRInitError Init(vr::EVRApplicationType eApplicationType, const char* pStartupInfo) = 0;
	virtual void Cleanup() = 0;
	virtual vr::EVRInitError IsInterfaceVersionValid(const char* pchInterfaceVersion) = 0;
	virtual void* GetGenericInterface(const char* pchNameAndVersion, vr::EVRInitError* peError) = 0;
	virtual bool BIsHmdPresent() = 0;
	virtual const char* GetEnglishStringForHmdError(vr::EVRInitError eError) = 0;
	virtual const char* GetIDForVRInitError(vr::EVRInitError eError) = 0;
};

// One JSON object per line, flushed after each, so a crash loses nothing written before it
class Writer {
public:
	explicit Writer(const std::string& path);
	~Writer();
	void Write(const Json::Value& record);

private:
	FILE* file = nullptr;
};

// crash_report.cpp: on an abnormal end, writes why and the stack to <outPath>.crash.txt
void InstallCrashReport(const std::string& outPath);

struct CaseContext {
	IVRClientCore* core = nullptr;
	CallContext* call = nullptr;
	Writer* out = nullptr;
	std::string caseName;
	std::string manifest; // --manifest, for input-manifest
	// Writes {"case", "method": label, fields...}
	void Record(const std::string& label, Json::Value fields = Json::Value(Json::objectValue));
};

using CaseFn = void (*)(CaseContext& ctx);
const std::map<std::string, CaseFn>& Cases();
const std::map<std::string, CaseFn>& GfxCases(); // gfx_cases.cpp, part of Cases()

} // namespace conformance
