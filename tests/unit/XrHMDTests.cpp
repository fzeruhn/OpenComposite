// doctest first: stdafx.h brings in Xlib on Linux, whose macros (None, Bool, ...) clash with doctest's names
#include "doctest.h"

#include "stdafx.h"

#include "XrHMD.h"

#include <cstddef>
#include <cstring>
#include <new>

TEST_CASE("Hidden_area_mesh_padding_zero")
{
	// Made where the game's copy lives, over whatever was there before: every byte not a member's is zero
	static const vr::HmdVector2_t vertices[3] = {};
	// Called through a pointer, as the game's call reaches it (not inlined into the copy)
	vr::HiddenAreaMesh_t (*volatile make)(const vr::HmdVector2_t*, uint32_t) = &XrHMD::HiddenAreaMesh;
	for (uint32_t count : { 0u, 3u }) {
		alignas(vr::HiddenAreaMesh_t) unsigned char storage[sizeof(vr::HiddenAreaMesh_t)];
		std::memset(storage, 0xAB, sizeof(storage));
		auto* mesh = new (storage) vr::HiddenAreaMesh_t(make(count ? vertices : nullptr, count));
		CHECK(mesh->pVertexData == (count ? vertices : nullptr));
		CHECK(mesh->unTriangleCount == count);
		size_t end = offsetof(vr::HiddenAreaMesh_t, unTriangleCount) + sizeof(mesh->unTriangleCount);
		for (size_t i = end; i < sizeof(storage); i++)
			CHECK(storage[i] == 0);
	}
}
