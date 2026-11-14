#include "stdafx.h"
#define BASE_IMPL
#include "BaseExtendedDisplay.h"

#include <string>

// Unreal Engine uses this for some silly reason
void BaseExtendedDisplay::GetWindowBounds(int32_t* pnX, int32_t* pnY, uint32_t* pnWidth, uint32_t* pnHeight)
{
	// Make up something that sounds about right
	if (pnX)
		*pnX = 0;
	if (pnY)
		*pnY = 0;
	if (pnWidth)
		*pnWidth = xr_main_view(XruEyeLeft).recommendedImageRectWidth * 2;
	if (pnHeight)
		*pnHeight = xr_main_view(XruEyeLeft).recommendedImageRectWidth;
}

void BaseExtendedDisplay::GetEyeOutputViewport(vr::EVREye eEye, uint32_t* pnX, uint32_t* pnY, uint32_t* pnWidth, uint32_t* pnHeight)
{
	// Each eye's half of the made-up window GetWindowBounds gives, left eye first
	uint32_t width = xr_main_view(XruEyeLeft).recommendedImageRectWidth;
	if (pnX)
		*pnX = eEye == vr::Eye_Right ? width : 0;
	if (pnY)
		*pnY = 0;
	if (pnWidth)
		*pnWidth = width;
	if (pnHeight)
		*pnHeight = width;
}

void BaseExtendedDisplay::GetDXGIOutputInfo(int32_t* pnAdapterIndex, int32_t* pnAdapterOutputIndex)
{
	// The headset isn't a desktop output: the first adapter's first output, as SteamVR answers in direct mode
	if (pnAdapterIndex)
		*pnAdapterIndex = 0;
	if (pnAdapterOutputIndex)
		*pnAdapterOutputIndex = 0;
}
