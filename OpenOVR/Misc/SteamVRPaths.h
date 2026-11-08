#pragma once

// Where SteamVR and Steam are, for the calls that answer with SteamVR's own files (IVRResources)

#include <filesystem>
#include <optional>

/**
 * SteamVR's install folder: the OpenVR paths file's runtime entry named SteamVR (as BaseClientCore reads it), else
 * <library>/steamapps/common/SteamVR in one of Steam's library folders (libraryfolders.vdf). It must contain bin/.
 * The overrides replace the paths file and Steam's folder (for tests).
 */
std::optional<std::filesystem::path> FindSteamVRInstall(const std::filesystem::path* pathsFileOverride = nullptr,
    const std::filesystem::path* steamDirOverride = nullptr);

/** Steam's config folder. Windows: HKCU\Software\Valve\Steam\SteamPath + \config; Linux: ~/.steam/steam/config */
std::optional<std::filesystem::path> FindSteamConfigDir();
