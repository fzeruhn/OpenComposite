#pragma once

// SteamVR's own settings files, read-only: the user's steamvr.vrsettings in Steam's config folder, then the
// defaults SteamVR ships in resources/settings/default.vrsettings. IVRSettings answers from them what OpenComposite's
// own store doesn't have.

#include "json/json.h"

#include <filesystem>
#include <optional>
#include <string>

/** section.key from the user's steamvr.vrsettings, else SteamVR's default.vrsettings; nothing if neither has it.
 * The files are cached and read again when they change. */
std::optional<Json::Value> SteamVRSettingValue(const std::string& section, const std::string& key);

/** section.key from the user's steamvr.vrsettings only: what the user chose, not SteamVR's defaults */
std::optional<Json::Value> SteamVRUserSettingValue(const std::string& section, const std::string& key);

/** Tests: read these files instead of the ones found through FindSteamConfigDir and FindSteamVRInstall. A path
 * that doesn't exist stands for a missing file. Called with nothing, they're found again. */
void SteamVRSettingsUseFiles(std::optional<std::filesystem::path> userFile = std::nullopt,
    std::optional<std::filesystem::path> defaultFile = std::nullopt);
