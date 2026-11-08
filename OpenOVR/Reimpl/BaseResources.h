#pragma once
#include "../BaseCommon.h"

/** SteamVR's shared resources (its own images and sounds), read from its install when there is one */
class BaseResources {
public:
	/** <SteamVR>/resources/<name>: its size, and the file copied when pchBuffer holds it all. 0 if there's none. */
	uint32_t LoadSharedResource(const char* pchResourceName, char* pchBuffer, uint32_t unBufferLen);

	/** <SteamVR>/resources/<directory>/<name>, whether or not the file is there (as SteamVR answers): the path's size
	 * including its terminator, copied when it fits. 0 and an empty string without a SteamVR install. */
	uint32_t GetResourceFullPath(const char* pchResourceName, const char* pchResourceTypeDirectory,
	    VR_OUT_STRING() char* pchPathBuffer, uint32_t unBufferLen);
};
