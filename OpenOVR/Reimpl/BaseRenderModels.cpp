#include "stdafx.h"
#include "Misc/EnumNames.h"
#include <numbers>
#define BASE_IMPL
#include "BaseRenderModels.h"
#include "Misc/Config.h"
#include "convert.h"
#include "generated/static_bases.gen.h"
#include "resources.h"

// Used for the hand offsets
#include "BaseCompositor.h"
#include "BaseInput.h"
#include "BaseSystem.h"
#include "Misc/Input/InteractionProfile.h"

#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtx/transform.hpp>

using glm::mat4;
using glm::quat;
using glm::vec3;
using glm::vec4;

#ifdef _MSC_VER
#pragma region structs
#endif

enum OOVR_EVRRenderModelError : int {
	VRRenderModelError_None = 0,
	VRRenderModelError_Loading = 100,
	VRRenderModelError_NotSupported = 200,
	VRRenderModelError_InvalidArg = 300,
	VRRenderModelError_InvalidModel = 301,
	VRRenderModelError_NoShapes = 302,
	VRRenderModelError_MultipleShapes = 303,
	VRRenderModelError_TooManyVertices = 304,
	VRRenderModelError_MultipleTextures = 305,
	VRRenderModelError_BufferTooSmall = 306,
	VRRenderModelError_NotEnoughNormals = 307,
	VRRenderModelError_NotEnoughTexCoords = 308,

	VRRenderModelError_InvalidTexture = 400,
};

struct OOVR_RenderModel_Vertex_t {
	vr::HmdVector3_t vPosition; // position in meters in device space
	vr::HmdVector3_t vNormal;
	float rfTextureCoord[2];
};

#if defined(__linux__) || defined(__APPLE__)
// This structure was originally defined mis-packed on Linux, preserved for
// compatibility.
#pragma pack(push, 4)
#endif

struct OOVR_RenderModel_t {
	const OOVR_RenderModel_Vertex_t* rVertexData; // Vertex data for the mesh
	uint32_t unVertexCount; // Number of vertices in the vertex data
	const uint16_t* rIndexData; // Indices into the vertex data for each triangle
	uint32_t unTriangleCount; // Number of triangles in the mesh. Index count is 3 * TriangleCount
	OOVR_TextureID_t diffuseTextureId; // Session unique texture identifier. Rendermodels which share the same texture will have the same id. <0 == texture not present
};

struct OOVR_RenderModel_TextureMap_t {
	uint16_t unWidth, unHeight; // width and height of the texture map in pixels
	const uint8_t* rubTextureMapData; // Map texture data. All textures are RGBA with 8 bits per channel per pixel. Data size is width * height * 4ub
};

#if defined(__linux__) || defined(__APPLE__)
#pragma pack(pop)
#endif

typedef uint32_t VRComponentProperties;
enum OOVR_EVRComponentProperty {
	VRComponentProperty_IsStatic = (1 << 0),
	VRComponentProperty_IsVisible = (1 << 1),
	VRComponentProperty_IsTouched = (1 << 2),
	VRComponentProperty_IsPressed = (1 << 3),
	VRComponentProperty_IsScrolled = (1 << 4),
};

#ifdef _MSC_VER
#pragma endregion
#endif

typedef OOVR_RenderModel_t RenderModel_t;
typedef OOVR_EVRRenderModelError EVRRenderModelError;
typedef OOVR_RenderModel_TextureMap_t RenderModel_TextureMap_t;
typedef OOVR_TextureID_t TextureID_t;

// The render models OpenComposite has (LoadRenderModel_Async), each with one component named after it
static bool IsKnownRenderModel(const char* name)
{
	static const char* const known[] = {
		"renderLeftHand",
		"renderRightHand",
		"oculusHmdRenderModel",
		"oculus_quest2_controller_left",
		"oculus_quest2_controller_right",
		"{indexcontroller}valve_controller_knu_1_0_left",
		"{indexcontroller}valve_controller_knu_1_0_right",
		"vive_tracker",
	};
	for (const char* k : known)
		if (strcmp(k, name) == 0)
			return true;
	return false;
}

// Copies value into an app's buffer, cut short and terminated if it doesn't fit; returns the size needed with the
// terminator, as SteamVR does
static uint32_t CopyOutModelString(const string& value, char* buffer, uint32_t size)
{
	if (buffer && size) {
		size_t n = std::min<size_t>(value.size(), size - 1);
		memcpy(buffer, value.data(), n);
		buffer[n] = 0;
	}
	return (uint32_t)value.size() + 1;
}

// The models and textures handed out and not yet freed: Free* ignores anything else (a pointer freed twice, or one
// that never came from here) instead of corrupting the heap. Never freed: a Free* call can come during static
// destruction
static std::mutex& LiveLock()
{
	static std::mutex& lock = *new std::mutex;
	return lock;
}
static std::set<const void*>& LiveModels()
{
	static std::set<const void*>& models = *new std::set<const void*>;
	return models;
}
static std::set<const void*>& LiveTextures()
{
	static std::set<const void*>& textures = *new std::set<const void*>;
	return textures;
}

static void AddLive(std::set<const void*>& set, const void* p)
{
	std::lock_guard<std::mutex> guard(LiveLock());
	set.insert(p);
}

static bool TakeLive(std::set<const void*>& set, const void* p)
{
	std::lock_guard<std::mutex> guard(LiveLock());
	return set.erase(p) != 0;
}

static string loadResource(int rid)
{
#ifndef _WIN32
	const char *start = nullptr, *end = nullptr;
	FindResourceLinux(rid, &start, &end);
	if (!start || !end)
		return {};
	return { start, (size_t)(end - start) };
#else
	// Open our OBJ file
	HRSRC ref = FindResource(openovr_module_id, MAKEINTRESOURCE(rid), MAKEINTRESOURCE(RES_T_OBJ));
	// An empty string if it can't be read: LoadRenderModel_Async then answers NotSupported
	if (!ref) {
		OOVR_LOGF("FindResource error: %lu", GetLastError());
		return {};
	}

	char* cstr = (char*)LoadResource(openovr_module_id, ref);
	if (!cstr) {
		OOVR_LOGF("LoadResource error: %lu", GetLastError());
		return {};
	}

	DWORD len = SizeofResource(openovr_module_id, ref);
	if (!len) {
		OOVR_LOGF("SizeofResource error: %lu", GetLastError());
		return {};
	}

	return string(cstr, len);
#endif
}

static OOVR_RenderModel_Vertex_t split_face(
    const string& s,
    const std::vector<vr::HmdVector3_t>& verts,
    const std::vector<vr::HmdVector2_t>& uvs,
    const std::vector<vr::HmdVector3_t>& normals)
{

	size_t slash1 = s.find('/');
	size_t slash2 = s.find('/', slash1 + 1);

	// The models are OpenComposite's own, so a bad face is a build problem: it becomes a zero vertex
	OOVR_RenderModel_Vertex_t out{};
	if (slash1 == string::npos || slash2 == string::npos) {
		OOVR_LOGF("Bad face spec: %s", s.c_str());
		return out;
	}

	int vert = atoi(s.substr(0, slash1).c_str());
	int uv = atoi(s.substr(slash1 + 1, slash2 - slash1 - 1).c_str());
	int norm = atoi(s.substr(slash2 + 1).c_str());

	// OBJ references start at one
	vert--;
	uv--;
	norm--;
	if (vert < 0 || uv < 0 || norm < 0 || (size_t)vert >= verts.size() || (size_t)uv >= uvs.size() || (size_t)norm >= normals.size()) {
		OOVR_LOGF("Face spec out of range: %s", s.c_str());
		return out;
	}

	// Build the result
	out.vPosition = verts[vert];
	out.vNormal = normals[norm];
	out.rfTextureCoord[0] = uvs[uv].v[0];
	out.rfTextureCoord[1] = uvs[uv].v[1];
	return out;
}

EVRRenderModelError BaseRenderModels::LoadRenderModel_Async(const char* pchRenderModelName, RenderModel_t** renderModel)
{
	if (!pchRenderModelName || !renderModel)
		return VRRenderModelError_InvalidArg;
	*renderModel = nullptr;
	OOVR_LOGF("LoadRenderModel_Async %s", pchRenderModelName);
	string name = pchRenderModelName;
	int rid;
	float sided;

	// todo: start loading correct models, !359 related
	if (name == "renderLeftHand") {
		rid = RES_O_HAND_LEFT;
		sided = 1;
	} else if (name == "renderRightHand") {
		rid = RES_O_HAND_RIGHT;
		sided = -1;
	} else if (name == "oculus_quest2_controller_left") {
		rid = RES_O_HAND_LEFT;
		sided = 1;
	} else if (name == "oculus_quest2_controller_right") {
		rid = RES_O_HAND_RIGHT;
		sided = -1;
	} else if (name == "{indexcontroller}valve_controller_knu_1_0_left") {
		rid = RES_O_HAND_LEFT;
		sided = 1;
	} else if (name == "{indexcontroller}valve_controller_knu_1_0_right") {
		rid = RES_O_HAND_RIGHT;
		sided = -1;
	} else if (name == "vive_tracker") {
		rid = RES_O_VIVE_TRACKER;
		sided = 1;
	} else if (name == "oculusHmdRenderModel") {
		// no model for the HMD
		return VRRenderModelError_NotSupported;
	} else {
		// A model OpenComposite doesn't have: games then draw none, or their own
		OC_UNSUPPORTED(VRRenderModelError_NotSupported, "LoadRenderModel_Async: no render model '%s'", pchRenderModelName);
	}

	string resource = loadResource(rid);
	if (resource.empty())
		return VRRenderModelError_NotSupported;
	std::istringstream res = std::istringstream(resource);

	std::vector<vr::HmdVector3_t> verts;
	std::vector<vr::HmdVector2_t> uvs;
	std::vector<vr::HmdVector3_t> normals;
	std::vector<OOVR_RenderModel_Vertex_t> vertexData;

	// Transform to line up the model with the Touch controller
	mat4 modelTransform = mat4(glm::rotate(sided * std::numbers::pi_v<float> / 2, vec3(0, 0, 1)));
	modelTransform[3] = vec4(sided * 0.015f, 0.0f, 0.03f, 1.0f);

	// SteamVR rotates it's models 180deg around the Y axis for some reason
	modelTransform *= mat4(glm::rotate(std::numbers::pi_v<float>, vec3(0, 1, 0)));

	mat4 transform = glm::inverse(rid == RES_O_VIVE_TRACKER ? BaseCompositor::GetTrackerTransform() : BaseCompositor::GetHandTransform()) * modelTransform;

	while (!res.eof()) {
		string op;
		res >> op;

		if (op == "v") {
			// Vertex
			vec3 v;
			res >> v.x >> v.y >> v.z;
			// Maya exports in cm, so translate that to meters
			v *= 0.01f;

			verts.push_back(G2S_v3f(v));
		} else if (op == "vt") {
			// UV
			float x, y;
			res >> x >> y;
			uvs.push_back(vr::HmdVector2_t{ x, y });
		} else if (op == "vn") {
			// Normal
			vec3 v;
			res >> v.x >> v.y >> v.z;

			normals.push_back(G2S_v3f(v));
		} else if (op == "f") {
			// Face
			string a, b, c;
			res >> a >> b >> c;

			vertexData.push_back(split_face(a, verts, uvs, normals));
			vertexData.push_back(split_face(b, verts, uvs, normals));
			vertexData.push_back(split_face(c, verts, uvs, normals));
		}
	}

	*renderModel = new RenderModel_t();
	RenderModel_t& rm = **renderModel;

	rm.unVertexCount = (uint32_t)vertexData.size();
	OOVR_RenderModel_Vertex_t* vertexData_arr = new OOVR_RenderModel_Vertex_t[rm.unVertexCount];
	rm.rVertexData = vertexData_arr;
	for (uint32_t i = 0; i < rm.unVertexCount; i++) {
		vec4 vertex = vec4(vertexData[i].vPosition.v[0], vertexData[i].vPosition.v[1], vertexData[i].vPosition.v[2], 1.0f);
		vertex = transform * vertex;
		vertexData_arr[i].vPosition.v[0] = vertex.x;
		vertexData_arr[i].vPosition.v[1] = vertex.y;
		vertexData_arr[i].vPosition.v[2] = vertex.z;
		vertexData_arr[i].vNormal = vertexData[i].vNormal;
		vertexData_arr[i].rfTextureCoord[0] = vertexData[i].rfTextureCoord[0];
		vertexData_arr[i].rfTextureCoord[1] = vertexData[i].rfTextureCoord[1];
	}

	uint16_t* indexData = new uint16_t[rm.unVertexCount];
	for (uint32_t i = 0; i < rm.unVertexCount; i++) {
		indexData[i] = static_cast<uint16_t>(i);
	}
	rm.rIndexData = indexData;
	rm.unTriangleCount = rm.unVertexCount / 3;
	AddLive(LiveModels(), *renderModel);

	// Texture
	rm.diffuseTextureId = -1; // Disabled for now

	return VRRenderModelError_None;
}

void BaseRenderModels::FreeRenderModel(RenderModel_t* renderModel)
{
	if (!renderModel || !TakeLive(LiveModels(), renderModel)) {
		OOVR_LOG_ONCE("FreeRenderModel: not a render model from LoadRenderModel_Async (or freed already), ignored");
		return;
	}
	delete[] renderModel->rVertexData;
	delete[] renderModel->rIndexData;
	delete renderModel;
}

EVRRenderModelError BaseRenderModels::LoadTexture_Async(TextureID_t textureId, RenderModel_TextureMap_t** texture)
{
	if (!texture)
		return VRRenderModelError_InvalidArg;
	*texture = new RenderModel_TextureMap_t();
	AddLive(LiveTextures(), *texture);
	RenderModel_TextureMap_t& tx = **texture;

	// For now use a 1x1 single coloured texture
	tx.unWidth = 1;
	tx.unHeight = 1;
	uint8_t* d = new uint8_t[tx.unWidth * tx.unHeight * 4];
	tx.rubTextureMapData = d;

	vr::HmdColor_t colour = oovr_global_configuration.HandColour();
	d[0] = (uint8_t)(colour.r * 255);
	d[1] = (uint8_t)(colour.g * 255);
	d[2] = (uint8_t)(colour.b * 255);
	d[3] = (uint8_t)(colour.a * 255);

	return VRRenderModelError_None;
}

void BaseRenderModels::FreeTexture(RenderModel_TextureMap_t* texture)
{
	if (!texture || !TakeLive(LiveTextures(), texture)) {
		OOVR_LOG_ONCE("FreeTexture: not a texture from LoadTexture_Async (or freed already), ignored");
		return;
	}
	delete[] texture->rubTextureMapData;
	delete texture;
}

EVRRenderModelError BaseRenderModels::LoadTextureD3D11_Async(TextureID_t textureId, void* pD3D11Device, void** ppD3D11Texture2D)
{
	if (ppD3D11Texture2D)
		*ppD3D11Texture2D = nullptr;
	OC_UNSUPPORTED(VRRenderModelError_NotSupported, "LoadTextureD3D11_Async: use LoadTexture_Async or LoadIntoTextureD3D11_Async");
}

EVRRenderModelError BaseRenderModels::LoadIntoTextureD3D11_Async(TextureID_t textureId, void* pDstTexture)
{
#ifndef SUPPORT_DX11
	OC_UNSUPPORTED(VRRenderModelError_NotSupported, "LoadIntoTextureD3D11_Async: this build has no D3D11 support");
#else
	ID3D11Texture2D* output = (ID3D11Texture2D*)pDstTexture;
	// Windows never maps the first 64 KiB, so a "texture" there is a bad argument, not a texture
	if ((uintptr_t)output < 0x10000)
		return VRRenderModelError_InvalidArg;

	struct pix_t {
		uint8_t r, g, b, a;
	};

	// Grab the desired colour
	vr::HmdColor_t colour = oovr_global_configuration.HandColour();

	pix_t pixColour = { (uint8_t)(colour.r * 255), (uint8_t)(colour.g * 255), (uint8_t)(colour.b * 255), 255 };

	// Since we don't know what the map flags are, make an identical texture and copy across

	// D3D11 setup
	ID3D11Device* device;
	ID3D11DeviceContext* context;
	output->GetDevice(&device);
	device->GetImmediateContext(&context);

	// Create the temporary texture
	D3D11_TEXTURE2D_DESC desc;
	output->GetDesc(&desc);

	// Quite conveniently, HmdColor_t happens to have the perfect layout for this
	int px_count = desc.Width * desc.Height;
	pix_t* pixels = new pix_t[px_count];
	for (int i = 0; i < px_count; i++) {
		pixels[i] = pixColour;
	}

	// Cross our fingers it's a four-byte RGBA format.
	int count = desc.MipLevels * desc.ArraySize;
	D3D11_SUBRESOURCE_DATA* init = new D3D11_SUBRESOURCE_DATA[count];
	for (int i = 0; i < count; i++) {
		init[i] = { pixels, sizeof(uint32_t), 0 };
	}

	ID3D11Texture2D* tempTex = nullptr;
	HRESULT hr = device->CreateTexture2D(&desc, init, &tempTex);
	delete[] init;
	delete[] pixels;
	if (FAILED(hr)) {
		// The app's texture keeps what it had
		context->Release();
		device->Release();
		OC_UNSUPPORTED(VRRenderModelError_InvalidTexture, "LoadIntoTextureD3D11_Async: CreateTexture2D failed (0x%08lx)", (unsigned long)hr);
	}

	// Copy over the texture
	context->CopyResource(output, tempTex);

	// Cleanup
	tempTex->Release();
	context->Release();
	device->Release();

	return VRRenderModelError_None;
#endif
}

void BaseRenderModels::FreeTextureD3D11(void* pD3D11Texture2D)
{
	// LoadTextureD3D11_Async hands no textures out
}

uint32_t BaseRenderModels::GetRenderModelName(uint32_t unRenderModelIndex, VR_OUT_STRING() char* pchRenderModelName, uint32_t unRenderModelNameLen)
{
	const char* renderModelName = nullptr;
	uint32_t strLen = 0;

	OOVR_LOGF("GetRenderModelName %d", unRenderModelIndex);

	switch (unRenderModelIndex) {
	case 0:
		renderModelName = "renderLeftHand";
		strLen = strlen(renderModelName);
		break;
	case 1:
		renderModelName = "renderRightHand";
		strLen = strlen(renderModelName);
		break;
	case 2:
		renderModelName = "vive_tracker";
		strLen = strlen(renderModelName);
		break;
	default:
		break;
	}

	if (pchRenderModelName && renderModelName && unRenderModelNameLen > 0) {
		strncpy(pchRenderModelName, renderModelName, unRenderModelNameLen - 1);
		pchRenderModelName[unRenderModelNameLen - 1] = '\0';
	}

	return strLen;
}

uint32_t BaseRenderModels::GetRenderModelCount()
{
	return 3;
}

uint32_t BaseRenderModels::GetComponentCount(const char* pchRenderModelName)
{
	// Left at zero for now until I can properly test it, and add textures
	return oovr_global_configuration.RenderCustomHands() ? 1 : 0;

	// This means there are no moving components (eg buttons thumbstick etc) which
	//  can be animated via the Component functions, which thus shouldn't be called.
}

uint32_t BaseRenderModels::GetComponentName(const char* pchRenderModelName, uint32_t unComponentIndex,
    char* pchComponentName, uint32_t unComponentNameLen)
{
	if (!pchRenderModelName || !IsKnownRenderModel(pchRenderModelName)) {
		if (pchComponentName && unComponentNameLen)
			pchComponentName[0] = 0;
		return 0;
	}

	// Only the first component exists
	if (unComponentIndex != 0) {
		if (pchComponentName && unComponentNameLen)
			pchComponentName[0] = 0;
		return 0;
	}

	// The component is named after its model; cut short if the buffer is too small, with the size needed returned
	return CopyOutModelString(pchRenderModelName, pchComponentName, unComponentNameLen);
}

uint64_t BaseRenderModels::GetComponentButtonMask(const char* pchRenderModelName, const char* pchComponentName)
{
	return 0;
}

uint32_t BaseRenderModels::GetComponentRenderModelName(const char* pchRenderModelName, const char* pchComponentName,
    char* componentModelName, uint32_t componentModelNameLen)
{
	if (!pchRenderModelName || !pchComponentName || !IsKnownRenderModel(pchRenderModelName)
	    || strcmp(pchRenderModelName, pchComponentName) != 0) {
		// Only the model's one component (named after it) has a render model
		if (componentModelName && componentModelNameLen)
			componentModelName[0] = 0;
		return 0;
	}

	return CopyOutModelString(pchRenderModelName, componentModelName, componentModelNameLen);
}

bool BaseRenderModels::GetComponentStateForDevicePath(const char* pchRenderModelName, const char* pchComponentName,
    vr::VRInputValueHandle_t devicePath, const OOVR_RenderModel_ControllerMode_State_t* pState,
    OOVR_RenderModel_ComponentState_t* pComponentState)
{
	// todo: make use of devicePath
	return GetComponentState(pchRenderModelName, pchComponentName, nullptr, pState, pComponentState);
}

bool BaseRenderModels::GetComponentState(const char* pchRenderModelName, const char* pchComponentName,
    const vr::VRControllerState_t* pControllerState, const OOVR_RenderModel_ControllerMode_State_t* pState,
    OOVR_RenderModel_ComponentState_t* pComponentState)
{
	// On the Quest 2 Touch controllers, here's SteamVR's poses for the base, handgrip and tip components:
	// Left  base:     mat4x4((-1.000000, 0.000000, 0.000000, 0.000000), (0.000000, 0.999976, 0.006981, 0.000000), (-0.000000, 0.006981, -0.999976, 0.000000), (-0.003400, -0.003400, 0.149100, 1.000000))
	// Right base:     mat4x4((-1.000000, 0.000000, 0.000000, 0.000000), (0.000000, 0.999976, 0.006981, 0.000000), (-0.000000, 0.006981, -0.999976, 0.000000), (0.003400, -0.003400, 0.149100, 1.000000))
	// Left  handgrip: mat4x4((1.000000, 0.000000, 0.000000, 0.000000), (0.000000, 0.996138, 0.087799, 0.000000), (0.000000, -0.087799, 0.996138, 0.000000), (0.000000, 0.003000, 0.097000, 1.000000))
	// Right handgrip: mat4x4((1.000000, 0.000000, 0.000000, 0.000000), (0.000000, 0.996138, 0.087799, 0.000000), (0.000000, -0.087799, 0.996138, 0.000000), (0.000000, 0.003000, 0.097000, 1.000000))
	// Left  tip:      mat4x4((1.000000, 0.000000, 0.000000, 0.000000), (0.000000, 0.794415, -0.607376, 0.000000), (0.000000, 0.607376, 0.794415, 0.000000), (0.016694, -0.025220, 0.024687, 1.000000))
	// Right tip:      mat4x4((1.000000, 0.000000, 0.000000, 0.000000), (0.000000, 0.794415, -0.607376, 0.000000), (0.000000, 0.607376, 0.794415, 0.000000), (-0.016694, -0.025220, 0.024687, 1.000000))

	if (!pComponentState)
		return false;
	ZeroMemory(pComponentState, sizeof(*pComponentState));
	if (!pchComponentName)
		return false;

	std::string componentName = pchComponentName;

	ITrackedDevice::TrackedDeviceType hand = ITrackedDevice::HAND_NONE;
	if (pchRenderModelName) {
		std::string renderModelName = pchRenderModelName;
		if (renderModelName == "renderLeftHand") {
			hand = ITrackedDevice::HAND_LEFT;
		} else if (renderModelName == "renderRightHand") {
			hand = ITrackedDevice::HAND_RIGHT;
		} else if (renderModelName == "vive_tracker") {
			hand = ITrackedDevice::GENERIC_TRACKER;
		} else {
			hand = ITrackedDevice::HAND_NONE;
		}
	}

	// See if we can get it properly
	bool success = TryGetComponentState(hand, componentName, pComponentState);
	if (success)
		return true;

	// Log a warning about this component, but only do so once
	if (!warnedAboutComponents.contains(componentName)) {
		OOVR_LOGF("Unknown component %s - returning a fake identity component", componentName.c_str());
		warnedAboutComponents.insert(componentName);
	}

	vr::HmdMatrix34_t ident{};
	ident.m[0][0] = ident.m[1][1] = ident.m[2][2] = 1;

	pComponentState->mTrackingToComponentLocal = ident;
	pComponentState->mTrackingToComponentRenderModel = ident;

	pComponentState->uProperties = VRComponentProperty_IsVisible | VRComponentProperty_IsStatic;

	return true;
}

bool BaseRenderModels::TryGetComponentState(ITrackedDevice::TrackedDeviceType hand, const std::string& componentName, OOVR_RenderModel_ComponentState_t* result)
{
	if (hand == ITrackedDevice::HAND_NONE)
		return false;

	std::shared_ptr<ITrackedDevice> dev = BackendManager::Instance().GetDeviceByHand(hand);
	if (!dev)
		return false;

	// See if there's a manually-defined transform
	const InteractionProfile* profile = dev->GetInteractionProfile();
	if (profile) {
		std::optional<glm::mat4> transform = profile->GetComponentTransform(hand, componentName);
		if (transform) {
			result->mTrackingToComponentLocal = G2S_m34(transform.value());
			result->mTrackingToComponentRenderModel = G2S_m34(transform.value());
			result->uProperties = VRComponentProperty_IsVisible | VRComponentProperty_IsStatic;
			return true;
		}
	}

	// If the hand position is offset, then this needs to account for it
	glm::mat4 handToGripSpace = glm::identity<glm::mat4>();
	if (profile) {
		handToGripSpace = glm::affineInverse(profile->GetGripToSteamVRTransform(hand));
	}

	// The grip space is simple - it's just the hand-to-grip transform matrix
	if (componentName == "handgrip") {
		result->mTrackingToComponentLocal = G2S_m34(handToGripSpace);
		result->mTrackingToComponentRenderModel = G2S_m34(handToGripSpace);
		result->uProperties = VRComponentProperty_IsVisible | VRComponentProperty_IsStatic;
		return true;
	}

	// If it's not manually specified, calculate the 'tip' position to match up with the OpenXR aim pose.
	if (componentName != "tip") {
		return false;
	}

	XrSpace componentSpace, gripSpace;
	GetBaseInput()->GetHandSpace(hand, componentSpace, true);
	GetBaseInput()->GetHandSpace(hand, gripSpace, false);

	if (!componentSpace || !gripSpace) {
		return false;
	}

	XrSpaceLocation location = { XR_TYPE_SPACE_LOCATION };
	if (!xr_gbl)
		return false;
	OC_XR_SOFT(xrLocateSpace(componentSpace, gripSpace, xr_gbl->GetBestTime(), &location), false);

	if ((location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) == 0) {
		// If the location is invalid, there's not really a lot we can do. Just use the
		// last known position as is currently in pose.
		OOVR_LOG_ONCE("Relative component location is not valid");
	}

	glm::mat4 gripToComponentSpace = X2G_om34_pose(location.pose);
	glm::mat4 handToComponentSpace = handToGripSpace * gripToComponentSpace;

	result->mTrackingToComponentLocal = G2S_m34(handToComponentSpace);
	result->mTrackingToComponentRenderModel = G2S_m34(handToComponentSpace);
	result->uProperties = VRComponentProperty_IsVisible | VRComponentProperty_IsStatic;
	return true;
}

bool BaseRenderModels::RenderModelHasComponent(const char* pchRenderModelName, const char* pchComponentName)
{
	// The models' one component is named after the model (GetComponentName)
	return pchRenderModelName && pchComponentName && IsKnownRenderModel(pchRenderModelName)
	    && strcmp(pchRenderModelName, pchComponentName) == 0;
}

uint32_t BaseRenderModels::GetRenderModelThumbnailURL(const char* pchRenderModelName, VR_OUT_STRING() char* pchThumbnailURL, uint32_t unThumbnailURLLen, EVRRenderModelError* peError)
{
	if (peError)
		*peError = VRRenderModelError_NotSupported;
	if (pchThumbnailURL && unThumbnailURLLen)
		pchThumbnailURL[0] = 0;
	// OpenComposite's models have no thumbnails
	return 0;
}

uint32_t BaseRenderModels::GetRenderModelOriginalPath(const char* pchRenderModelName, VR_OUT_STRING() char* pchOriginalPath, uint32_t unOriginalPathLen, EVRRenderModelError* peError)
{
	if (peError)
		*peError = VRRenderModelError_NotSupported;
	if (pchOriginalPath && unOriginalPathLen)
		pchOriginalPath[0] = 0;
	// OpenComposite's models are built into the DLL: no path
	return 0;
}

const char* BaseRenderModels::GetRenderModelErrorNameFromEnum(EVRRenderModelError error)
{
	// As SteamVR answers
	return OC_EnumNameAsSteamVR("EVRRenderModelError", (int32_t)error, "Unknown render model error (%d)", true);
}
