#pragma once

// Application manifests (.vrmanifest files) as SteamVR reads them, and starting the programs they name

#include "json/json.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

class SharedStore;

struct AppManifestEntry {
	std::string appKey, launchType, binaryPath, arguments, workingDirectory, url, imagePath, actionManifestPath, nameEnUs, descriptionEnUs, source, newsUrl;
	bool isDashboardOverlay = false, isTemplate = false, isInstanced = false, isInternal = false, wantsCompositorPauseInStandby = false, isHidden = false;
	std::vector<std::string> mimeTypes;
	std::filesystem::path manifestPath;

	/** binaryPath, resolved against the manifest's folder if it's relative (empty if there's none) */
	std::filesystem::path ResolvedBinary() const;
	/** workingDirectory resolved the same way; the manifest's folder if there's none */
	std::filesystem::path ResolvedWorkingDirectory() const;
	/** actionManifestPath and imagePath, resolved the same way (empty if there's none) */
	std::filesystem::path Resolved(const std::string& path) const;

	/** As kept in the store's Applications document, and back */
	Json::Value ToJson() const;
	static AppManifestEntry FromJson(const Json::Value& value);
};

/** The applications in a manifest file (a BOM, comments and trailing commas are fine). An application without an
 * app_key is left out with a warning; a file that can't be read or has no applications array gives none, with a
 * warning saying why. */
std::vector<AppManifestEntry> ParseAppManifest(const std::filesystem::path& path, std::vector<std::string>& warnings);

/** This process's app key, in order: what IdentifyApplication set for it, steam.app.<SteamAppId>, the installed
 * application whose binary is this program, else system.generated.<exe file name, lower-cased> */
std::string CurrentProcessAppKey();
std::string CurrentProcessAppKey(SharedStore& store);

/** The applications of every manifest in the store's Applications document, a temporary one only while the
 * process that added it lives; by key */
std::vector<AppManifestEntry> InstalledApplications(SharedStore& store);

/** This program's own path */
std::filesystem::path CurrentExecutablePath();

/** Text as a path, from UTF-8; empty if it isn't UTF-8 */
std::filesystem::path PathFromUtf8(const std::string& text);
std::string PathToUtf8(const std::filesystem::path& path);

/** A command line's arguments, split as CommandLineToArgvW does: whitespace separates, double quotes group, and
 * backslashes escape only before a double quote. No shell syntax: & $ ; ` and the like are plain text. */
std::vector<std::string> SplitCommandLine(const std::string& commandLine);
/** One argument quoted so SplitCommandLine (and a Windows program) reads it back unchanged */
std::string QuoteArgument(const std::string& argument);

/** Starts binary with the arguments (one command line, as the manifest has them, split by SplitCommandLine on Linux)
 * in workingDirectory, not waiting for it. Its process id, or nothing if it didn't start. */
std::optional<uint32_t> LaunchProcess(const std::filesystem::path& binary, const std::string& arguments,
    const std::filesystem::path& workingDirectory);

/** Opens a URL with the desktop's handler (ShellExecuteW, xdg-open). False if that couldn't be started. */
bool OpenUrl(const std::string& url);
