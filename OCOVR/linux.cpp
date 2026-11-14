//
// Created by ZNix on 14/02/2021.
//

#include "stdafx.h"

#include "plat.h"

alternativeCoreFactory_t PlatformGetAlternativeCoreFactory()
{
	// The per-app runtime choice is Windows-only (BaseClientCore::CheckAppEnabled), so this isn't reached
	OOVR_LOG("Choosing SteamVR per app isn't supported on Linux; using OpenLamina");
	return nullptr;
}
