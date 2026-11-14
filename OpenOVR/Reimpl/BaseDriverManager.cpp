#include "stdafx.h"
#define BASE_IMPL
#include "BaseDriverManager.h"

#include "Misc/xrutil.h"

#include <cctype>
#include <cstring>

std::string BaseDriverManager::DriverName()
{
	if (xr_instance == XR_NULL_HANDLE)
		return "openxr";
	XrInstanceProperties properties = { XR_TYPE_INSTANCE_PROPERTIES };
	if (XR_FAILED(xrGetInstanceProperties(xr_instance, &properties)))
		return "openxr";
	std::string name;
	for (const char* c = properties.runtimeName; *c && !isspace((unsigned char)*c); c++)
		name += (char)tolower((unsigned char)*c);
	return name.empty() ? "openxr" : name;
}

uint32_t BaseDriverManager::GetDriverCount() const
{
	return 1;
}

uint32_t BaseDriverManager::GetDriverName(vr::DriverId_t nDriver, char* pchValue, uint32_t unBufferSize)
{
	// As SteamVR: the size the name needs, with its terminator; copied when it fits, else nothing written
	if (nDriver != 0) {
		if (pchValue && unBufferSize)
			pchValue[0] = '\0';
		return 0;
	}
	std::string name = DriverName();
	uint32_t needed = (uint32_t)name.size() + 1;
	if (pchValue && unBufferSize >= needed)
		memcpy(pchValue, name.c_str(), needed);
	return needed;
}

vr::DriverHandle_t BaseDriverManager::GetDriverHandle(const char* pchDriverName)
{
	return pchDriverName && DriverName() == pchDriverName ? 1 : vr::k_ulInvalidDriverHandle;
}

bool BaseDriverManager::IsEnabled(vr::DriverId_t nDriver) const
{
	return nDriver == 0;
}
