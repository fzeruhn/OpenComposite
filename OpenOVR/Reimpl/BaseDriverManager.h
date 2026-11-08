#pragma once
#include "../BaseCommon.h"

#include <string>

/** One driver: the OpenXR runtime, named by the first word of its name, lower-cased ("monado"); "openxr" before Init */
class BaseDriverManager {
public:
	uint32_t GetDriverCount() const;
	uint32_t GetDriverName(vr::DriverId_t nDriver, VR_OUT_STRING() char* pchValue, uint32_t unBufferSize);
	vr::DriverHandle_t GetDriverHandle(const char* pchDriverName);
	bool IsEnabled(vr::DriverId_t nDriver) const;

	/** The driver's name: the runtime's first word lower-cased, "openxr" if there's no runtime (yet) */
	static std::string DriverName();
};
