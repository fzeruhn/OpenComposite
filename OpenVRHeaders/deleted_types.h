#pragma once
// Types the oldest SDK headers (0.9.0 to 0.9.11) use that later ones renamed or deleted. split_headers.py copies this
// next to the generated interfaces, and every generated header includes it. Each is defined as the old header did,
// for the old interface versions' signatures.

#include "vrtypes.h"

// Also in OpenOVR/custom_types.h, which OpenComposite's own code includes
#ifndef OC_HAVE_EGRAPHICSAPICONVENTION
#define OC_HAVE_EGRAPHICSAPICONVENTION
enum EGraphicsAPIConvention {
	API_DirectX = 0, // Normalized Z goes from 0 at the viewer to 1 at the far clip plane
	API_OpenGL = 1, // Normalized Z goes from 1 at the viewer to -1 at the far clip plane
};
#endif

// The SDK packs its structs to 4 on Linux and macOS (openvr.h), 8 elsewhere: these are its structs
#if defined(__linux__) || defined(__APPLE__)
#pragma pack(push, 4)
#else
#pragma pack(push, 8)
#endif

namespace vr {

// Renamed: the values are today's (Eye_Left 0, Eye_Right 1; TrackingUniverseSeated 0, Standing 1, RawAndUncalibrated 2)
typedef EVREye Hmd_Eye;
typedef ETrackingUniverseOrigin TrackingUniverseOrigin;
typedef ::EGraphicsAPIConvention GraphicsAPIConvention;
typedef EVRSubmitFlags VRSubmitFlags_t;
typedef EVRFirmwareError VRFirmwareError;
// These were later renamed with an E. Their values are the same where both have a name (each is an int either way);
// the few that differ (0.9.0's TrackedDeviceClass_Other 1000, Prop_DisplayFirmwareVersion as a string) aren't converted.
typedef ETrackedDeviceClass TrackedDeviceClass;
typedef ETrackedDeviceProperty TrackedDeviceProperty;
typedef ETrackedPropertyError TrackedPropertyError;
typedef EVROverlayError VROverlayError;

// IVRCompositor_005-007's overlay settings (in vrtypes.h until 1.9.16, unused after 0.9.0)
struct Compositor_OverlaySettings {
	uint32_t size; // sizeof(Compositor_OverlaySettings)
	bool curved, antialias;
	float scale, distance, alpha;
	float uOffset, vOffset, uScale, vScale;
	float gridDivs, gridWidth, gridScale;
	HmdMatrix44_t transform;
};

// The render model of IVRSystem_003 (0.9.0) and IVRRenderModels_001 (0.9.2 to 0.9.11), which carried its texture
// inline. Later versions define their own RenderModel_t, so these go in the old versions' namespaces only, where
// nothing else sees them.
namespace IVRRenderModels_001 {
	struct RenderModel_Vertex_t {
		HmdVector3_t vPosition; // position in meters in device space
		HmdVector3_t vNormal;
		float rfTextureCoord[2];
	};

	struct RenderModel_TextureMap_t {
		uint16_t unWidth, unHeight; // width and height of the texture map in pixels
		const uint8_t* rubTextureMapData; // RGBA, 8 bits per channel per pixel; width * height * 4 bytes
	};

	struct RenderModel_t {
		uint64_t ulInternalHandle; // Used internally by SteamVR
		const RenderModel_Vertex_t* rVertexData; // Vertex data for the mesh
		uint32_t unVertexCount; // Number of vertices in the vertex data
		const uint16_t* rIndexData; // Indices into the vertex data for each triangle
		uint32_t unTriangleCount; // Number of triangles in the mesh. Index count is 3 * TriangleCount
		RenderModel_TextureMap_t diffuseTexture; // RGBA diffuse texture for the model
	};
} // namespace IVRRenderModels_001

namespace IVRSystem_003 {
	using IVRRenderModels_001::RenderModel_t;
	using IVRRenderModels_001::RenderModel_TextureMap_t;
	using IVRRenderModels_001::RenderModel_Vertex_t;
} // namespace IVRSystem_003

} // namespace vr

#pragma pack(pop)
