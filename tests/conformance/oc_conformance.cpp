// oc_conformance: loads a vrclient DLL (OpenComposite's or SteamVR's) and calls every method of one interface version
// with one argument set, or runs one named case (cases.cpp), writing a JSON line per call. See README.md.
//
//   oc_conformance --runtime <vrclient dll> --interface <IVRName_NNN> --apptype <n> --argset valid|edge|null|invalid
//                  [--start-at <i>] --out <file.jsonl> [--real-runtime] [--data <dir>]
//   oc_conformance --runtime <dll> --case <name> --out <file.jsonl> [--real-runtime] [--data <dir>] [--manifest <file>]

#include "conformance.h"

#ifdef _WIN32
#include <windows.h>
#pragma comment(lib, "dxgi.lib") // CreateDXGIFactory1
#else
#include <dlfcn.h>
#endif

#include <cstdlib>
#include <cstring>

using namespace conformance;

Writer::Writer(const std::string& path)
{
	file = std::fopen(path.c_str(), "ab");
}

Writer::~Writer()
{
	if (file)
		std::fclose(file);
}

void Writer::Write(const Json::Value& record)
{
	if (!file)
		return;
	Json::StreamWriterBuilder wb;
	wb["indentation"] = "";
	std::string line = Json::writeString(wb, record);
	std::fwrite(line.data(), 1, line.size(), file);
	std::fputc('\n', file);
	std::fflush(file);
}

void CaseContext::Record(const std::string& label, Json::Value fields)
{
	fields["case"] = caseName;
	fields["method"] = label;
	out->Write(fields);
}

typedef void* (*VRClientCoreFactoryFn)(const char* pInterfaceName, int* pReturnCode);

static VRClientCoreFactoryFn LoadFactory(const std::string& path)
{
#ifdef _WIN32
	int len = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
	std::wstring wide(len, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wide.data(), len);
	HMODULE dll = LoadLibraryW(wide.c_str());
	if (!dll)
		return nullptr;
	return (VRClientCoreFactoryFn)GetProcAddress(dll, "VRClientCoreFactory");
#else
	void* dll = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
	if (!dll)
		return nullptr;
	return (VRClientCoreFactoryFn)dlsym(dll, "VRClientCoreFactory");
#endif
}

static ArgSet ParseArgSet(const std::string& s)
{
	if (s == "edge")
		return ArgSet::Edge;
	if (s == "null")
		return ArgSet::Null;
	if (s == "invalid")
		return ArgSet::Invalid;
	return ArgSet::Valid;
}

int main(int argc, char** argv)
{
	std::map<std::string, std::string> args;
	for (int i = 1; i < argc; i++) {
		std::string a = argv[i];
		if (a.rfind("--", 0) != 0)
			continue;
		if (a == "--real-runtime") {
			args[a] = "1";
			continue;
		}
		if (i + 1 < argc)
			args[a] = argv[++i];
	}
	if (!args.count("--runtime") || !args.count("--out") || !(args.count("--interface") || args.count("--case"))) {
		std::fprintf(stderr, "usage: see the top of oc_conformance.cpp\n");
		return 2;
	}

	InstallCrashReport(args["--out"]);
	Writer out(args["--out"]);
	CallContext ctx;
	ctx.againstRealRuntime = args.count("--real-runtime") != 0;
	ctx.dataDir = args.count("--data") ? args["--data"] : OC_TEST_DATA_DIR;
#ifdef _WIN32
	// The first adapter's, as an OpenXR runtime names it for D3D (one on another adapter is refused); WARP without one
	{
		IDXGIFactory1* factory = nullptr;
		IDXGIAdapter1* adapter = nullptr;
		if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory)) && factory->EnumAdapters1(0, &adapter) == S_OK) {
			D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &ctx.d3d11, nullptr, nullptr);
			adapter->Release();
		}
		if (factory)
			factory->Release();
	}
	if (!ctx.d3d11)
		D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &ctx.d3d11, nullptr, nullptr);
#endif

	VRClientCoreFactoryFn factory = LoadFactory(args["--runtime"]);
	if (!factory) {
		Json::Value r;
		r["error"] = "could not load the runtime or find VRClientCoreFactory";
		out.Write(r);
		return 3;
	}
	int rc = 0;
	auto* core = (IVRClientCore*)factory("IVRClientCore_003", &rc);
	if (!core) {
		Json::Value r;
		r["error"] = "no IVRClientCore_003";
		r["factory_error"] = rc;
		out.Write(r);
		return 3;
	}

	if (args.count("--case")) {
		auto it = Cases().find(args["--case"]);
		if (it == Cases().end()) {
			std::fprintf(stderr, "unknown case %s\n", args["--case"].c_str());
			return 2;
		}
		CaseContext cc;
		cc.core = core;
		cc.call = &ctx;
		cc.out = &out;
		cc.caseName = args["--case"];
		cc.manifest = args.count("--manifest") ? args["--manifest"] : (ctx.dataDir / "manifest_basic.json").string();
		it->second(cc);
		return 0;
	}

	std::string version = args["--interface"];
	int apptype = std::atoi(args.count("--apptype") ? args["--apptype"].c_str() : "1");
	std::string argsetName = args.count("--argset") ? args["--argset"] : "valid";
	ArgSet set = ParseArgSet(argsetName);
	int startAt = std::atoi(args.count("--start-at") ? args["--start-at"].c_str() : "0");

	vr::EVRInitError initError = core->Init((vr::EVRApplicationType)apptype, nullptr);
	vr::EVRInitError lookupError = vr::VRInitError_None;
	void* iface = core->GetGenericInterface(version.c_str(), &lookupError);

	auto base = [&](Json::Value r) {
		r["interface"] = version;
		r["apptype"] = apptype;
		r["argset"] = argsetName;
		r["init_error"] = (int)initError;
		r["lookup_error"] = (int)lookupError;
		return r;
	};

	const std::vector<CallerFn>* callers = FindCallers(version.c_str());
	if (!iface || !callers) {
		Json::Value r;
		r["method"] = "<lookup>";
		r["index"] = -1;
		r["found"] = iface != nullptr;
		r["callers"] = callers != nullptr;
		out.Write(base(r));
		if (initError == vr::VRInitError_None)
			core->Cleanup();
		return 0;
	}
	for (int i = startAt; i < (int)callers->size(); i++) {
		CallRecord cr = (*callers)[i](iface, set, ctx);
		Json::Value r;
		r["method"] = cr.method;
		r["index"] = cr.index;
		r["ret"] = cr.ret;
		r["out"] = cr.out;
		if (!cr.skipped.empty())
			r["skipped"] = cr.skipped;
		out.Write(base(r));
	}
	if (initError == vr::VRInitError_None)
		core->Cleanup();
	return 0;
}
