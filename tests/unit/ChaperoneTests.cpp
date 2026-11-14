// doctest first: stdafx.h brings in Xlib on Linux, whose macros (None, Bool, ...) clash with doctest's names
#include "doctest.h"

#include "stdafx.h"

#include "Misc/ChaperoneData.h"

TEST_CASE("Chaperone_data_round_trip")
{
	ChaperoneData d = ChaperoneData::FromStage(4, 5);
	REQUIRE(d.collisionBounds.size() == 1);
	CHECK(d.collisionBounds[0].vCorners[0].v[0] == -2.0f);
	CHECK(d.collisionBounds[0].vCorners[2].v[2] == 2.5f);
	d.tags = { 1, 0 };
	d.physicalBounds.push_back(ChaperoneData::FloorQuad(6, 7));
	d.perimeter = { { { 1, 2 } }, { { -3, 4.5f } } };
	d.standingZero.m[0][3] = 0.25f;
	d.seatedZero.m[1][3] = 1.5f;
	d.colors[0] = { 1, 0.5f, 0, 1 };
	d.colors[1] = { 0, 0, 1, 0.5f };

	std::optional<ChaperoneData> back = ChaperoneData::FromJson(d.ToJson());
	REQUIRE(back.has_value());
	CHECK(back->playArea[0] == 4.0f);
	CHECK(back->playArea[1] == 5.0f);
	REQUIRE(back->collisionBounds.size() == 1);
	for (int c = 0; c < 4; c++)
		for (int i = 0; i < 3; i++)
			CHECK(back->collisionBounds[0].vCorners[c].v[i] == d.collisionBounds[0].vCorners[c].v[i]);
	CHECK(back->tags == d.tags);
	REQUIRE(back->physicalBounds.size() == 1);
	CHECK(back->physicalBounds[0].vCorners[2].v[0] == 3.0f);
	REQUIRE(back->perimeter.size() == 2);
	CHECK(back->perimeter[1].v[0] == -3.0f);
	CHECK(back->perimeter[1].v[1] == 4.5f);
	CHECK(back->standingZero.m[0][3] == 0.25f);
	CHECK(back->standingZero.m[0][0] == 1.0f);
	CHECK(back->seatedZero.m[1][3] == 1.5f);
	CHECK(back->colors[0].g == 0.5f);
	CHECK(back->colors[1].a == 0.5f);

	// Not a room setup
	CHECK_FALSE(ChaperoneData::FromJson(Json::Value(Json::objectValue)).has_value());
	CHECK_FALSE(ChaperoneData::FromJson(Json::Value("garbage")).has_value());
	// A broken quad is left out, not half read
	Json::Value broken = d.ToJson();
	broken["collision_bounds"][0][1] = "x";
	CHECK(ChaperoneData::FromJson(broken)->collisionBounds.empty());
	// A tag that isn't a number (an imported buffer's, say) reads as 0
	broken["tags"][0] = "x";
	broken["tags"][1] = -1;
	CHECK(ChaperoneData::FromJson(broken)->tags == std::vector<uint8_t>{ 0, 0 });
}
