#pragma once
// Names of OpenVR enum values, generated from the OpenVR headers by scripts/gen_enum_names.py (EnumNames.gen.cpp).

#include <cstdint>

// The name of value in the enum named enumType ("EVRInitError", "EVREventType", ...), or nullptr if it has none
const char* OC_EnumName(const char* enumType, int32_t value);
