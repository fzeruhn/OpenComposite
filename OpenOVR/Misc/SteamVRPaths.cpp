#include "stdafx.h"

#include "SteamVRPaths.h"

#include "Misc/JsonFile.h"

#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <shlobj.h>
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {

fs::path FromUtf8(const std::string& s)
{
	return fs::path(std::u8string(s.begin(), s.end()));
}

bool IsDirectory(const fs::path& p)
{
	std::error_code ec;
	return fs::is_directory(p, ec);
}

// A SteamVR install has its bin folder
bool IsSteamVRInstall(const fs::path& p)
{
	return IsDirectory(p / "bin");
}

std::optional<fs::path> DefaultPathsFile()
{
#ifdef _WIN32
	WCHAR appData[MAX_PATH];
	if (!SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, appData)))
		return std::nullopt;
	return fs::path(appData) / "openvr" / "openvrpaths.vrpath";
#else
	// As BaseClientCore: the XDG config folder, else ~/.config
	const char* xdg = getenv("XDG_CONFIG_HOME");
	if (xdg && xdg[0])
		return fs::path(xdg) / "openvr" / "openvrpaths.vrpath";
	const char* home = getenv("HOME");
	if (!home || !home[0])
		return std::nullopt;
	return fs::path(home) / ".config" / "openvr" / "openvrpaths.vrpath";
#endif
}

std::optional<fs::path> DefaultSteamDir()
{
#ifdef _WIN32
	WCHAR value[MAX_PATH];
	DWORD size = sizeof(value);
	if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, nullptr, value, &size) != ERROR_SUCCESS)
		return std::nullopt;
	return fs::path(value);
#else
	const char* home = getenv("HOME");
	if (!home || !home[0])
		return std::nullopt;
	for (const char* sub : { ".steam/steam", ".local/share/Steam" }) {
		fs::path p = fs::path(home) / sub;
		if (IsDirectory(p))
			return p;
	}
	return std::nullopt;
#endif
}

// The paths file's runtime entries named SteamVR (case aside, trailing slashes aside)
std::optional<fs::path> FromPathsFile(const fs::path& pathsFile)
{
	Json::Value root;
	if (!oc_read_json_file(pathsFile, root))
		return std::nullopt;
	const Json::Value& runtimes = root["runtime"];
	if (!runtimes.isArray())
		return std::nullopt;
	for (const Json::Value& entry : runtimes) {
		if (!entry.isString())
			continue;
		fs::path p = FromUtf8(entry.asString());
		while (!p.empty() && !p.has_filename() && p.has_parent_path() && p.parent_path() != p)
			p = p.parent_path();
		std::string name = p.filename().string();
		for (char& c : name)
			c = (char)tolower((unsigned char)c);
		if (name == "steamvr" && IsSteamVRInstall(p))
			return p;
	}
	return std::nullopt;
}

// Steam's library folders: Steam's own and each "path" in steamapps/libraryfolders.vdf
std::optional<fs::path> FromSteamLibraries(const fs::path& steamDir)
{
	std::vector<fs::path> libraries = { steamDir };
	std::ifstream vdf(steamDir / "steamapps" / "libraryfolders.vdf", std::ios::binary);
	if (vdf) {
		std::stringstream text;
		text << vdf.rdbuf();
		std::string contents = text.str();
		static const std::regex pathEntry(R"re("path"\s+"((?:[^"\\]|\\.)*)")re");
		for (std::sregex_iterator it(contents.begin(), contents.end(), pathEntry), end; it != end; ++it) {
			std::string raw = (*it)[1].str(), unescaped;
			for (size_t i = 0; i < raw.size(); i++) {
				if (raw[i] == '\\' && i + 1 < raw.size())
					i++;
				unescaped += raw[i];
			}
			libraries.push_back(FromUtf8(unescaped));
		}
	}
	for (const fs::path& library : libraries) {
		fs::path p = library / "steamapps" / "common" / "SteamVR";
		if (IsSteamVRInstall(p))
			return p;
	}
	return std::nullopt;
}

} // namespace

std::optional<fs::path> FindSteamVRInstall(const fs::path* pathsFileOverride, const fs::path* steamDirOverride)
{
	std::optional<fs::path> pathsFile = pathsFileOverride ? std::optional<fs::path>(*pathsFileOverride) : DefaultPathsFile();
	if (pathsFile) {
		if (auto found = FromPathsFile(*pathsFile))
			return found;
	}
	std::optional<fs::path> steamDir = steamDirOverride ? std::optional<fs::path>(*steamDirOverride) : DefaultSteamDir();
	if (steamDir)
		return FromSteamLibraries(*steamDir);
	return std::nullopt;
}

std::optional<fs::path> FindSteamConfigDir()
{
	std::optional<fs::path> steamDir = DefaultSteamDir();
	if (!steamDir || !IsDirectory(*steamDir / "config"))
		return std::nullopt;
	return *steamDir / "config";
}
