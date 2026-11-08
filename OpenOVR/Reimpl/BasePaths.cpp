#include "stdafx.h"
#define BASE_IMPL
#include "BasePaths.h"

#include <cstring>

vr::ETrackedPropertyError BasePaths::StringToHandle(OOVR_PathHandle_t* pHandle, const char* pchPath)
{
	if (!pHandle || !pchPath)
		return vr::TrackedProp_InvalidOperation;
	std::lock_guard<std::mutex> guard(lock);
	auto it = handles.find(pchPath);
	if (it == handles.end()) {
		paths.emplace_back(pchPath);
		it = handles.emplace(pchPath, (OOVR_PathHandle_t)paths.size()).first;
	}
	*pHandle = it->second;
	return vr::TrackedProp_Success;
}

vr::ETrackedPropertyError BasePaths::HandleToString(OOVR_PathHandle_t pHandle, char* pchBuffer, uint32_t unBufferSize, uint32_t* punBufferSizeUsed)
{
	if (punBufferSizeUsed)
		*punBufferSizeUsed = 0;
	if (pchBuffer && unBufferSize)
		pchBuffer[0] = '\0';
	std::string path;
	{
		std::lock_guard<std::mutex> guard(lock);
		if (pHandle == 0 || pHandle > paths.size())
			return vr::TrackedProp_InvalidOperation;
		path = paths[pHandle - 1];
	}
	uint32_t needed = (uint32_t)path.size() + 1;
	if (punBufferSizeUsed)
		*punBufferSizeUsed = needed;
	if (!pchBuffer || unBufferSize < needed)
		return vr::TrackedProp_BufferTooSmall;
	memcpy(pchBuffer, path.c_str(), needed);
	return vr::TrackedProp_Success;
}
