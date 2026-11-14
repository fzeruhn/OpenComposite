#pragma once

// The room setup as IVRChaperoneSetup keeps it: a working copy that programs change, and the committed copy in
// the store's Chaperone document that every program reads

#include "json/json.h"

#include "generated/interfaces/vrtypes.h"

#include <cstdint>
#include <optional>
#include <vector>

struct ChaperoneData {
	float playArea[2] = { 0, 0 }; // width (X) and depth (Z), metres
	std::vector<vr::HmdQuad_t> collisionBounds;
	std::vector<uint8_t> tags; // one per collision bounds quad
	std::vector<vr::HmdQuad_t> physicalBounds;
	std::vector<vr::HmdVector2_t> perimeter;
	vr::HmdMatrix34_t standingZero = Identity();
	vr::HmdMatrix34_t seatedZero = Identity();
	vr::HmdColor_t colors[2] = {}; // the bounds' colour, and the camera's

	/** The room the OpenXR runtime has: a play area of width by depth around the origin, with one collision bounds
	 * quad on the floor at its edges */
	static ChaperoneData FromStage(float width, float depth);
	/** The collision bounds quad on the floor at the edges of a width by depth play area around the origin */
	static vr::HmdQuad_t FloorQuad(float width, float depth);

	Json::Value ToJson() const;
	/** Nothing if it isn't an object with a play area; missing parts are empty */
	static std::optional<ChaperoneData> FromJson(const Json::Value& value);

	static vr::HmdMatrix34_t Identity();
};
