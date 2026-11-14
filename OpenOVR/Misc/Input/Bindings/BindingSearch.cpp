#include "stdafx.h"

#include "BindingSearch.h"

#include "Misc/AppManifest.h"

#include <cstdlib>
#include <cstring>
#include <system_error>

namespace fs = std::filesystem;

namespace {
bool IsFile(const fs::path& p)
{
	std::error_code ec;
	return !p.empty() && fs::is_regular_file(p, ec);
}
} // namespace

std::optional<fs::path> FindBindingFile(const std::string& appKey, const std::string& controllerType, const fs::path& userBindingsDir,
    const fs::path& workingDir, const fs::path& manifestDir, const std::string& defaultBindingUrl)
{
	std::string file = controllerType + ".json";
	// A controller type is one file name (a profile without one is named by its path, which isn't)
	bool oneName = !controllerType.empty() && controllerType.find_first_of("/\\") == std::string::npos;
	// The app key is one folder name, dots and all (steam.app.620980)
	if (oneName && !userBindingsDir.empty() && !appKey.empty() && appKey.find_first_of("/\\") == std::string::npos && appKey != "." && appKey != "..") {
		fs::path user = userBindingsDir / PathFromUtf8(appKey) / PathFromUtf8(file);
		if (IsFile(user))
			return user;
	}
	if (oneName && !workingDir.empty()) {
		fs::path local = workingDir / "OpenComposite" / PathFromUtf8(file);
		if (IsFile(local))
			return local;
	}
	if (!defaultBindingUrl.empty()) {
		fs::path def = manifestDir / PathFromUtf8(defaultBindingUrl);
		if (IsFile(def))
			return def;
	}
	return std::nullopt;
}

fs::path UserBindingsDir()
{
#ifdef _WIN32
	// Wide: the narrow variable is in the ANSI code page, which can't hold every folder name
	if (const wchar_t* overrideDir = _wgetenv(L"OC_BINDINGS_DIR"); overrideDir && overrideDir[0])
		return fs::path(overrideDir);
	if (const wchar_t* local = _wgetenv(L"LOCALAPPDATA"); local && local[0])
		return fs::path(local) / "OpenComposite" / "bindings";
	return {};
#else
	if (const char* overrideDir = getenv("OC_BINDINGS_DIR"); overrideDir && overrideDir[0])
		return PathFromUtf8(overrideDir);
	const char* xdg = getenv("XDG_STATE_HOME");
	if (xdg && xdg[0])
		return fs::path(xdg) / "OpenComposite" / "bindings";
	const char* home = getenv("HOME");
	if (home && home[0])
		return fs::path(home) / ".local" / "state" / "OpenComposite" / "bindings";
	return {};
#endif
}
