#include "stdafx.h"
#define GENFILE
#include "BaseCommon.h"

GEN_INTERFACE("Chaperone", "002")
GEN_INTERFACE("Chaperone", "003")
GEN_INTERFACE("Chaperone", "004")

#include "generated/GVRChaperone.gen.h"

// Changed in place (split_headers.py's KNOWN_INPLACE_CHANGES): a 0.9.11 or 0.9.12 caller passes no camera colour
// pointer, so only the colours are written
void CVRChaperone_003::GetBoundsColor(vr::HmdColor_t* pOutputColorArray, int nNumOutputColors, float flCollisionBoundsFadeDistance,
    vr::HmdColor_t* pOutputCameraColor)
{
	base->GetBoundsColor(pOutputColorArray, nNumOutputColors);
}
