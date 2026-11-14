#include "stdafx.h"
#define GENFILE
#include "BaseCommon.h"

GEN_INTERFACE("Input", "004")
GEN_INTERFACE("Input", "005")
GEN_INTERFACE("Input", "006")
GEN_INTERFACE("Input", "007")
// version 8 and 9 are skipped
GEN_INTERFACE("Input", "010")
GEN_INTERFACE("Input", "011")

#include "generated/GVRInput.gen.h"

// Prior to IVRInput_005 / OpenVR 1.1.3 the input layer had a slightly different format, so adapt that
vr::EVRInputError CVRInput_004::GetSkeletalActionData(vr::VRActionHandle_t action, vr::IVRInput_004::InputSkeletalActionData_t* pActionData, uint32_t unActionDataSize, vr::VRInputValueHandle_t ulRestrictToDevice)
{
	// A null or wrong-sized struct is an argument error, as the main method answers
	if (!pActionData || unActionDataSize != sizeof(vr::IVRInput_004::InputSkeletalActionData_t))
		return vr::VRInputError_InvalidParam;
	// Set the size to what the main method is expecting
	unActionDataSize = sizeof(OOVR_InputSkeletalActionData_t);

	// Load the required value (the main method's answer then says whether the action is usable)
	pActionData->boneCount = 0;
	base->GetBoneCount(action, &pActionData->boneCount);

	return base->GetSkeletalActionData(action, (OOVR_InputSkeletalActionData_t*)pActionData, unActionDataSize, ulRestrictToDevice);
}
