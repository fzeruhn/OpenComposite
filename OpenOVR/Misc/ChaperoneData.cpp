#include "stdafx.h"

#include "ChaperoneData.h"

namespace {

Json::Value Vec3(const vr::HmdVector3_t& v)
{
	Json::Value a(Json::arrayValue);
	for (float f : v.v)
		a.append(f);
	return a;
}

bool ReadFloats(const Json::Value& a, float* out, unsigned n)
{
	if (!a.isArray() || a.size() != n)
		return false;
	for (unsigned i = 0; i < n; i++) {
		if (!a[i].isNumeric())
			return false;
		out[i] = a[i].asFloat();
	}
	return true;
}

Json::Value Quads(const std::vector<vr::HmdQuad_t>& quads)
{
	Json::Value a(Json::arrayValue);
	for (const vr::HmdQuad_t& q : quads) {
		Json::Value corners(Json::arrayValue);
		for (const vr::HmdVector3_t& c : q.vCorners)
			corners.append(Vec3(c));
		a.append(corners);
	}
	return a;
}

std::vector<vr::HmdQuad_t> ReadQuads(const Json::Value& a)
{
	std::vector<vr::HmdQuad_t> quads;
	if (!a.isArray())
		return quads;
	for (const Json::Value& q : a) {
		vr::HmdQuad_t quad{};
		bool ok = q.isArray() && q.size() == 4;
		for (unsigned i = 0; ok && i < 4; i++)
			ok = ReadFloats(q[i], quad.vCorners[i].v, 3);
		if (ok)
			quads.push_back(quad);
	}
	return quads;
}

Json::Value Matrix(const vr::HmdMatrix34_t& m)
{
	Json::Value a(Json::arrayValue);
	for (const auto& row : m.m)
		for (float f : row)
			a.append(f);
	return a;
}

vr::HmdMatrix34_t ReadMatrix(const Json::Value& a)
{
	vr::HmdMatrix34_t m = ChaperoneData::Identity();
	float f[12];
	if (ReadFloats(a, f, 12)) {
		for (int r = 0; r < 3; r++)
			for (int c = 0; c < 4; c++)
				m.m[r][c] = f[r * 4 + c];
	}
	return m;
}

} // namespace

vr::HmdMatrix34_t ChaperoneData::Identity()
{
	vr::HmdMatrix34_t m{};
	m.m[0][0] = m.m[1][1] = m.m[2][2] = 1;
	return m;
}

vr::HmdQuad_t ChaperoneData::FloorQuad(float width, float depth)
{
	// Half the width and depth each side of the origin, like the play area
	float x = width / 2, z = depth / 2;
	vr::HmdQuad_t q{};
	q.vCorners[0] = { -x, 0, -z };
	q.vCorners[1] = { -x, 0, z };
	q.vCorners[2] = { x, 0, z };
	q.vCorners[3] = { x, 0, -z };
	return q;
}

ChaperoneData ChaperoneData::FromStage(float width, float depth)
{
	ChaperoneData d;
	d.playArea[0] = width;
	d.playArea[1] = depth;
	d.collisionBounds.push_back(FloorQuad(width, depth));
	return d;
}

Json::Value ChaperoneData::ToJson() const
{
	Json::Value v(Json::objectValue);
	v["play_area"] = Json::Value(Json::arrayValue);
	v["play_area"].append(playArea[0]);
	v["play_area"].append(playArea[1]);
	v["collision_bounds"] = Quads(collisionBounds);
	v["tags"] = Json::Value(Json::arrayValue);
	for (uint8_t t : tags)
		v["tags"].append(t);
	v["physical_bounds"] = Quads(physicalBounds);
	v["perimeter"] = Json::Value(Json::arrayValue);
	for (const vr::HmdVector2_t& p : perimeter) {
		Json::Value point(Json::arrayValue);
		point.append(p.v[0]);
		point.append(p.v[1]);
		v["perimeter"].append(point);
	}
	v["standing_zero"] = Matrix(standingZero);
	v["seated_zero"] = Matrix(seatedZero);
	v["colors"] = Json::Value(Json::arrayValue);
	for (const vr::HmdColor_t& c : colors) {
		Json::Value color(Json::arrayValue);
		color.append(c.r);
		color.append(c.g);
		color.append(c.b);
		color.append(c.a);
		v["colors"].append(color);
	}
	return v;
}

std::optional<ChaperoneData> ChaperoneData::FromJson(const Json::Value& v)
{
	ChaperoneData d;
	if (!v.isObject() || !ReadFloats(v["play_area"], d.playArea, 2))
		return std::nullopt;
	d.collisionBounds = ReadQuads(v["collision_bounds"]);
	if (v["tags"].isArray())
		for (const Json::Value& t : v["tags"])
			d.tags.push_back(t.isUInt() ? (uint8_t)t.asUInt() : 0);
	d.physicalBounds = ReadQuads(v["physical_bounds"]);
	if (v["perimeter"].isArray()) {
		for (const Json::Value& p : v["perimeter"]) {
			vr::HmdVector2_t point{};
			if (ReadFloats(p, point.v, 2))
				d.perimeter.push_back(point);
		}
	}
	d.standingZero = ReadMatrix(v["standing_zero"]);
	d.seatedZero = ReadMatrix(v["seated_zero"]);
	if (v["colors"].isArray()) {
		for (unsigned i = 0; i < 2 && i < v["colors"].size(); i++) {
			float c[4];
			if (ReadFloats(v["colors"][i], c, 4))
				d.colors[i] = { c[0], c[1], c[2], c[3] };
		}
	}
	return d;
}
