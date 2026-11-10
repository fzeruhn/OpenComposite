#include "stdafx.h"

#include "AppManifest.h"

#include "Misc/JsonFile.h"
#include "Misc/Process.h"
#include "Misc/SharedStore.h"

#include <algorithm>
#include <cstdlib>
#include <map>

#ifdef _WIN32
#include <shellapi.h>
#include <windows.h>
#else
#include <dlfcn.h>
#include <errno.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
extern char** environ;
#endif

namespace fs = std::filesystem;

fs::path PathFromUtf8(const std::string& text)
{
	try {
		return fs::path(std::u8string(text.begin(), text.end()));
	} catch (const std::exception&) {
		return {};
	}
}

std::string PathToUtf8(const fs::path& path)
{
	std::u8string s = path.u8string();
	return std::string(s.begin(), s.end());
}

namespace {

fs::path ResolveAgainst(const fs::path& manifest, const std::string& path)
{
	if (path.empty())
		return {};
	fs::path p = PathFromUtf8(path);
	if (p.is_relative())
		p = manifest.parent_path() / p;
	return p.lexically_normal();
}

std::string StringMember(const Json::Value& app, const char* name)
{
	const Json::Value& v = app[name];
	return v.isString() ? v.asString() : std::string();
}

bool BoolMember(const Json::Value& app, const char* name)
{
	const Json::Value& v = app[name];
	return v.isBool() ? v.asBool() : (v.isNumeric() && v.asDouble() != 0);
}

// ASCII only: the bytes of a UTF-8 character stay as they are
std::string Lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c; });
	return s;
}

} // namespace

fs::path AppManifestEntry::ResolvedBinary() const
{
	return ResolveAgainst(manifestPath, binaryPath);
}

fs::path AppManifestEntry::ResolvedWorkingDirectory() const
{
	if (workingDirectory.empty())
		return manifestPath.parent_path();
	return ResolveAgainst(manifestPath, workingDirectory);
}

fs::path AppManifestEntry::Resolved(const std::string& path) const
{
	return ResolveAgainst(manifestPath, path);
}

Json::Value AppManifestEntry::ToJson() const
{
	Json::Value v(Json::objectValue);
	v["app_key"] = appKey;
	v["launch_type"] = launchType;
	v["binary_path"] = binaryPath;
	v["arguments"] = arguments;
	v["working_directory"] = workingDirectory;
	v["url"] = url;
	v["image_path"] = imagePath;
	v["action_manifest_path"] = actionManifestPath;
	v["name"] = nameEnUs;
	v["description"] = descriptionEnUs;
	v["source"] = source;
	v["news_url"] = newsUrl;
	v["is_dashboard_overlay"] = isDashboardOverlay;
	v["is_template"] = isTemplate;
	v["is_instanced"] = isInstanced;
	v["is_internal"] = isInternal;
	v["wants_compositor_pause_in_standby"] = wantsCompositorPauseInStandby;
	v["is_hidden"] = isHidden;
	Json::Value mimes(Json::arrayValue);
	for (const std::string& m : mimeTypes)
		mimes.append(m);
	v["mime_types"] = mimes;
	v["manifest_path"] = PathToUtf8(manifestPath);
	return v;
}

AppManifestEntry AppManifestEntry::FromJson(const Json::Value& v)
{
	AppManifestEntry e;
	if (!v.isObject())
		return e;
	e.appKey = StringMember(v, "app_key");
	e.launchType = StringMember(v, "launch_type");
	e.binaryPath = StringMember(v, "binary_path");
	e.arguments = StringMember(v, "arguments");
	e.workingDirectory = StringMember(v, "working_directory");
	e.url = StringMember(v, "url");
	e.imagePath = StringMember(v, "image_path");
	e.actionManifestPath = StringMember(v, "action_manifest_path");
	e.nameEnUs = StringMember(v, "name");
	e.descriptionEnUs = StringMember(v, "description");
	e.source = StringMember(v, "source");
	e.newsUrl = StringMember(v, "news_url");
	e.isDashboardOverlay = BoolMember(v, "is_dashboard_overlay");
	e.isTemplate = BoolMember(v, "is_template");
	e.isInstanced = BoolMember(v, "is_instanced");
	e.isInternal = BoolMember(v, "is_internal");
	e.wantsCompositorPauseInStandby = BoolMember(v, "wants_compositor_pause_in_standby");
	e.isHidden = BoolMember(v, "is_hidden");
	if (v["mime_types"].isArray())
		for (const Json::Value& m : v["mime_types"])
			if (m.isString())
				e.mimeTypes.push_back(m.asString());
	e.manifestPath = PathFromUtf8(StringMember(v, "manifest_path"));
	return e;
}

std::vector<AppManifestEntry> ParseAppManifest(const fs::path& path, std::vector<std::string>& warnings)
{
	std::vector<AppManifestEntry> entries;
	Json::Value root;
	std::string error;
	if (!ReadJsonFileLenient(path, root, error)) {
		warnings.push_back(error);
		return entries;
	}
	if (!root.isObject() || !root["applications"].isArray()) {
		warnings.push_back(PathToUtf8(path) + ": no applications array");
		return entries;
	}

	fs::path absolute = path;
	std::error_code ec;
	if (absolute.is_relative())
		absolute = fs::absolute(path, ec);

	std::string source = StringMember(root, "source");
	for (const Json::Value& app : root["applications"]) {
		if (!app.isObject() || !app["app_key"].isString() || app["app_key"].asString().empty()) {
			warnings.push_back(PathToUtf8(path) + ": an application without an app_key, left out");
			continue;
		}
		AppManifestEntry e;
		e.manifestPath = absolute;
		e.appKey = app["app_key"].asString();
		e.launchType = StringMember(app, "launch_type");
		if (e.launchType.empty())
			e.launchType = "binary";
		// The platform's own binary first, as SteamVR picks it
#ifdef _WIN32
		e.binaryPath = StringMember(app, "binary_path_windows");
#else
		e.binaryPath = StringMember(app, "binary_path_linux");
#endif
		if (e.binaryPath.empty())
			e.binaryPath = StringMember(app, "binary_path");
		e.arguments = StringMember(app, "arguments");
		e.workingDirectory = StringMember(app, "working_directory");
		e.url = StringMember(app, "url");
		e.imagePath = StringMember(app, "image_path");
		e.actionManifestPath = StringMember(app, "action_manifest_path");
		e.newsUrl = StringMember(app, "news_url");
		e.source = source;
		e.isDashboardOverlay = BoolMember(app, "is_dashboard_overlay");
		e.isTemplate = BoolMember(app, "is_template");
		e.isInstanced = BoolMember(app, "is_instanced");
		e.isInternal = BoolMember(app, "is_internal");
		e.wantsCompositorPauseInStandby = BoolMember(app, "wants_compositor_pause_in_standby");
		e.isHidden = BoolMember(app, "is_hidden");
		if (app["mime_types"].isArray()) {
			for (const Json::Value& m : app["mime_types"])
				if (m.isString())
					e.mimeTypes.push_back(m.asString());
		}
		const Json::Value& strings = app["strings"];
		if (strings.isObject()) {
			// en_us, else the first language there is
			const Json::Value* lang = strings.isMember("en_us") ? &strings["en_us"] : nullptr;
			if (!lang && !strings.getMemberNames().empty())
				lang = &strings[strings.getMemberNames().front()];
			if (lang && lang->isObject()) {
				e.nameEnUs = StringMember(*lang, "name");
				e.descriptionEnUs = StringMember(*lang, "description");
			}
		}
		entries.push_back(std::move(e));
	}
	return entries;
}

std::vector<AppManifestEntry> InstalledApplications(SharedStore& store)
{
	std::map<std::string, AppManifestEntry> byKey;
	Json::Value doc = store.Read(StoreDoc::Applications);
	const Json::Value& manifests = doc["manifests"];
	if (manifests.isObject()) {
		for (const std::string& path : manifests.getMemberNames()) {
			const Json::Value& m = manifests[path];
			if (!m.isObject())
				continue;
			if (m["temporary"].isBool() && m["temporary"].asBool()
			    && !SharedStore::IsProcessAlive(m["owner_pid"].isUInt() ? m["owner_pid"].asUInt() : 0))
				continue;
			if (!m["apps"].isArray())
				continue;
			for (const Json::Value& app : m["apps"]) {
				AppManifestEntry e = AppManifestEntry::FromJson(app);
				if (!e.appKey.empty())
					byKey.emplace(e.appKey, std::move(e));
			}
		}
	}
	std::vector<AppManifestEntry> apps;
	for (auto& [key, e] : byKey)
		apps.push_back(std::move(e));
	return apps;
}

fs::path CurrentExecutablePath()
{
#ifdef _WIN32
	std::wstring buffer(MAX_PATH, L'\0');
	for (;;) {
		DWORD n = GetModuleFileNameW(nullptr, buffer.data(), (DWORD)buffer.size());
		if (n == 0)
			return {};
		if (n < buffer.size()) {
			buffer.resize(n);
			return fs::path(buffer);
		}
		buffer.resize(buffer.size() * 2);
	}
#else
	std::error_code ec;
	fs::path p = fs::read_symlink("/proc/self/exe", ec);
	return ec ? fs::path() : p;
#endif
}

std::string CurrentProcessAppKey(SharedStore& store)
{
	uint32_t pid = oc_current_pid();

	// 1. IdentifyApplication's
	Json::Value doc = store.Read(StoreDoc::Applications);
	const Json::Value& pids = doc["pids"];
	std::string pidKey = std::to_string(pid);
	if (pids.isObject() && pids[pidKey].isString() && !pids[pidKey].asString().empty())
		return pids[pidKey].asString();

	// 2. Steam's
	const char* appId = getenv("SteamAppId");
	if (appId && appId[0])
		return std::string("steam.app.") + appId;

	// 3. The installed application whose binary this is
	fs::path self = CurrentExecutablePath();
	std::error_code ec;
	if (!self.empty()) {
		for (const AppManifestEntry& e : InstalledApplications(store)) {
			fs::path binary = e.ResolvedBinary();
			if (!binary.empty() && fs::equivalent(binary, self, ec))
				return e.appKey;
		}
	}

	// 4. A key of its own
	return "system.generated." + Lower(PathToUtf8(self.filename()));
}

std::string CurrentProcessAppKey()
{
	return CurrentProcessAppKey(SharedStore::Get());
}

std::vector<std::string> SplitCommandLine(const std::string& commandLine)
{
	std::vector<std::string> args;
	std::string arg;
	bool inArg = false, quoted = false;
	for (size_t i = 0; i < commandLine.size();) {
		char c = commandLine[i];
		if (!quoted && (c == ' ' || c == '\t')) {
			if (inArg)
				args.push_back(arg);
			arg.clear();
			inArg = false;
			i++;
			continue;
		}
		inArg = true;
		if (c == '\\') {
			// 2n backslashes and a quote: n and the quote groups; 2n+1: n and a quote; else as they are
			size_t n = 0;
			for (; i < commandLine.size() && commandLine[i] == '\\'; i++)
				n++;
			bool beforeQuote = i < commandLine.size() && commandLine[i] == '"';
			arg.append(beforeQuote ? n / 2 : n, '\\');
			if (beforeQuote && n % 2) {
				arg += '"';
				i++;
			}
			continue;
		}
		if (c == '"') {
			if (quoted && i + 1 < commandLine.size() && commandLine[i + 1] == '"') {
				arg += '"'; // "" within quotes
				i += 2;
				continue;
			}
			quoted = !quoted;
			i++;
			continue;
		}
		arg += c;
		i++;
	}
	if (inArg)
		args.push_back(arg);
	return args;
}

std::string QuoteArgument(const std::string& argument)
{
	std::string out = "\"";
	size_t backslashes = 0;
	for (char c : argument) {
		if (c == '\\') {
			backslashes++;
			continue;
		}
		// Backslashes before a quote are doubled, and the quote escaped
		out.append(c == '"' ? backslashes * 2 + 1 : backslashes, '\\');
		backslashes = 0;
		out += c;
	}
	out.append(backslashes * 2, '\\'); // before the closing quote
	return out + "\"";
}

#ifndef _WIN32
// 0 without glibc
#define OC_GLIBC_AT_LEAST(minor) (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= (minor)))

namespace {

// Keeps this library loaded for good: a game may unload it while a thread waits for a child in its code
void StayLoaded()
{
	static int anchor;
	static const bool pinned = [] {
		Dl_info info{};
		return dladdr(&anchor, &info) && info.dli_fname && dlopen(info.dli_fname, RTLD_NOW | RTLD_NOLOAD | RTLD_NODELETE);
	}();
	(void)pinned;
}

// Waits for the child on a thread of its own, so it doesn't stay a zombie, which IsProcessAlive takes for a running
// program. Only for this pid, and SIGCHLD left alone: the game may wait for children of its own.
void ReapWhenDone(pid_t pid)
{
	StayLoaded();
	try {
		std::thread([pid] {
			int status = 0;
			while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
			}
		}).detach();
	} catch (const std::exception&) {
		// No thread: it stays a zombie until this program ends
	}
}

// Starts file (searched in PATH if search) with argv, in dir unless that's empty, without this program's descriptors
// past stderr (only with glibc 2.34 and later, which can close them)
std::optional<pid_t> Spawn(const std::string& file, const std::vector<std::string>& args, const fs::path& dir, bool search)
{
	std::vector<char*> argv;
	for (const std::string& a : args)
		argv.push_back(const_cast<char*>(a.c_str()));
	argv.push_back(nullptr);

	posix_spawn_file_actions_t actions;
	if (posix_spawn_file_actions_init(&actions) != 0)
		return std::nullopt;
#if OC_GLIBC_AT_LEAST(29)
	if (!dir.empty())
		posix_spawn_file_actions_addchdir_np(&actions, dir.c_str());
#else
	if (!dir.empty())
		OOVR_LOGF("Starting %s in this program's working folder: this C library can't choose another", file.c_str());
#endif
#if OC_GLIBC_AT_LEAST(34)
	posix_spawn_file_actions_addclosefrom_np(&actions, 3);
#endif
	// No signal blocked, and the default handling of those this program may ignore or handle itself
	posix_spawnattr_t attr;
	if (posix_spawnattr_init(&attr) != 0) {
		posix_spawn_file_actions_destroy(&actions);
		return std::nullopt;
	}
	sigset_t mask, defaults;
	sigemptyset(&mask);
	sigemptyset(&defaults);
	for (int sig : { SIGPIPE, SIGINT, SIGTERM, SIGCHLD })
		sigaddset(&defaults, sig);
	posix_spawnattr_setsigmask(&attr, &mask);
	posix_spawnattr_setsigdefault(&attr, &defaults);
	posix_spawnattr_setflags(&attr, (short)(POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF));
	pid_t pid = 0;
	int error = search ? posix_spawnp(&pid, file.c_str(), &actions, &attr, argv.data(), environ)
	                   : posix_spawn(&pid, file.c_str(), &actions, &attr, argv.data(), environ);
	posix_spawnattr_destroy(&attr);
	posix_spawn_file_actions_destroy(&actions);
	if (error != 0) {
		OOVR_LOGF("Couldn't start %s: %s", file.c_str(), strerror(error));
		return std::nullopt;
	}
	ReapWhenDone(pid);
	return pid;
}

} // namespace
#endif

std::optional<uint32_t> LaunchProcess(const fs::path& binary, const std::string& arguments, const fs::path& workingDirectory)
{
	if (binary.empty())
		return std::nullopt;
#ifdef _WIN32
	std::wstring commandLine = L"\"" + binary.wstring() + L"\"";
	if (!arguments.empty())
		commandLine += L" " + PathFromUtf8(arguments).wstring();
	STARTUPINFOW si{};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi{};
	std::wstring dir = workingDirectory.wstring();
	if (!CreateProcessW(binary.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr,
	        dir.empty() ? nullptr : dir.c_str(), &si, &pi)) {
		DWORD error = GetLastError();
		OOVR_LOGF("Couldn't start %s: error %lu", PathToUtf8(binary).c_str(), error);
		return std::nullopt;
	}
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return (uint32_t)pi.dwProcessId;
#else
	std::error_code ec;
	if (!fs::is_regular_file(binary, ec)) {
		OOVR_LOGF("Couldn't start %s: no such file", PathToUtf8(binary).c_str());
		return std::nullopt;
	}
	// The binary itself, with the arguments split here: no shell reads them
	std::vector<std::string> argv = SplitCommandLine(arguments);
	argv.insert(argv.begin(), binary.native());
	std::optional<pid_t> pid = Spawn(binary.native(), argv, workingDirectory, false);
	if (!pid)
		return std::nullopt;
	return (uint32_t)*pid;
#endif
}

bool OpenUrl(const std::string& url)
{
	if (url.empty())
		return false;
#ifdef _WIN32
	HINSTANCE result = ShellExecuteW(nullptr, L"open", PathFromUtf8(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	return (INT_PTR)result > 32;
#else
	return Spawn("xdg-open", { "xdg-open", url }, {}, true).has_value();
#endif
}
