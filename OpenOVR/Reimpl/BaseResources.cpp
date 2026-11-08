#include "stdafx.h"
#define BASE_IMPL
#include "BaseResources.h"

#include "Misc/SteamVRPaths.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

static fs::path FromUtf8(const char* s)
{
	std::string str = s ? s : "";
	return fs::path(std::u8string(str.begin(), str.end()));
}

// A resource name is relative and stays inside the resources folder
static bool Contained(const fs::path& name)
{
	if (name.empty() || name.is_absolute() || name.has_root_name())
		return false;
	for (const fs::path& part : name)
		if (part == "..")
			return false;
	return true;
}

uint32_t BaseResources::LoadSharedResource(const char* pchResourceName, char* pchBuffer, uint32_t unBufferLen)
{
	fs::path name = FromUtf8(pchResourceName);
	std::optional<fs::path> steamvr = FindSteamVRInstall();
	if (!steamvr || !Contained(name))
		return 0;
	std::ifstream file(*steamvr / "resources" / name, std::ios::binary | std::ios::ate);
	if (!file)
		return 0;
	std::streamoff size = file.tellg();
	if (size <= 0 || size > UINT32_MAX)
		return 0;
	if (pchBuffer && unBufferLen >= (uint64_t)size) {
		file.seekg(0);
		file.read(pchBuffer, size);
	}
	return (uint32_t)size;
}

uint32_t BaseResources::GetResourceFullPath(const char* pchResourceName, const char* pchResourceTypeDirectory,
    char* pchPathBuffer, uint32_t unBufferLen)
{
	if (pchPathBuffer && unBufferLen)
		pchPathBuffer[0] = '\0';
	if (!pchResourceName)
		return 0;
	std::optional<fs::path> steamvr = FindSteamVRInstall();
	if (!steamvr)
		return 0;
	// As SteamVR: the path the resource would have, whether or not it's there
	fs::path full = *steamvr / "resources" / FromUtf8(pchResourceTypeDirectory) / FromUtf8(pchResourceName);
	std::u8string u8 = full.make_preferred().u8string();
	std::string path(u8.begin(), u8.end());
	uint32_t needed = (uint32_t)path.size() + 1;
	if (pchPathBuffer && unBufferLen >= needed)
		memcpy(pchPathBuffer, path.c_str(), needed);
	return needed;
}
