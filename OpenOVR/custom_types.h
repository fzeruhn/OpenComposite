#pragma once

// Add types here that were deleted from OpenVR

// Also in OpenVRHeaders/deleted_types.h, which the generated headers include
#ifndef OC_HAVE_EGRAPHICSAPICONVENTION
#define OC_HAVE_EGRAPHICSAPICONVENTION
enum EGraphicsAPIConvention {
	API_DirectX = 0, // Normalized Z goes from 0 at the viewer to 1 at the far clip plane
	API_OpenGL = 1, // Normalized Z goes from 1 at the viewer to -1 at the far clip plane
};
#endif

enum EDualAnalogWhich {
	k_EDualAnalog_Left = 0,
	k_EDualAnalog_Right = 1,
};
