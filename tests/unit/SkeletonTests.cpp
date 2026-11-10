// doctest first: stdafx.h brings in Xlib on Linux, whose macros (None, Bool, ...) clash with doctest's names
#include "doctest.h"

#include "stdafx.h"

#include "Reimpl/BaseInput.h"

#include <cmath>
#include <vector>

namespace {
// A hand in parent space: every bone turned a little about a different axis and moved along it
std::vector<vr::VRBoneTransform_t> TestHand()
{
	std::vector<vr::VRBoneTransform_t> bones(31);
	for (int i = 0; i < 31; i++) {
		float angle = 0.1f + 0.03f * (float)i;
		float ax = (float)((i % 3) == 0), ay = (float)((i % 3) == 1), az = (float)((i % 3) == 2);
		float s = std::sin(angle / 2);
		bones[i].orientation = { std::cos(angle / 2), ax * s, ay * s, az * s };
		bones[i].position = { 0.01f * (float)i, 0.02f, -0.03f * (float)(i % 5), 1.0f };
	}
	return bones;
}

void CheckSame(const std::vector<vr::VRBoneTransform_t>& a, const vr::VRBoneTransform_t* b)
{
	for (int i = 0; i < 31; i++) {
		for (int c = 0; c < 3; c++)
			CHECK(std::fabs(a[i].position.v[c] - b[i].position.v[c]) < 1e-5f);
		CHECK(std::fabs(a[i].orientation.w - b[i].orientation.w) < 1e-5f);
		CHECK(std::fabs(a[i].orientation.x - b[i].orientation.x) < 1e-5f);
		CHECK(std::fabs(a[i].orientation.y - b[i].orientation.y) < 1e-5f);
		CHECK(std::fabs(a[i].orientation.z - b[i].orientation.z) < 1e-5f);
	}
}
} // namespace

TEST_CASE("Skeleton_has_31_bones_root_first")
{
	CHECK(HandBoneName(0, true) == "Root");
	CHECK(HandBoneParent(0) == -1);
	CHECK(HandBoneParent(1) == 0);
	// A parent is always before its child
	for (vr::BoneIndex_t i = 1; i < 31; i++) {
		CHECK(HandBoneParent(i) >= 0);
		CHECK(HandBoneParent(i) < i);
	}
	CHECK(HandBoneParent(eBone_IndexFinger0) == eBone_Wrist);
	CHECK(HandBoneParent(eBone_IndexFinger4) == eBone_IndexFinger3);
	CHECK(HandBoneParent(eBone_Aux_PinkyFinger) == eBone_Root);
	CHECK(HandBoneParent(31) == -1);
	CHECK(HandBoneName(31, true).empty());
	CHECK(HandBoneName(-1, true).empty());
}

TEST_CASE("Bone_names_carry_hand_suffix")
{
	CHECK(HandBoneName(eBone_Wrist, true) == "wrist_r");
	CHECK(HandBoneName(eBone_Wrist, false) == "wrist_l");
	CHECK(HandBoneName(eBone_IndexFinger0, false) == "finger_index_meta_l");
	CHECK(HandBoneName(eBone_IndexFinger4, true) == "finger_index_r_end");
	CHECK(HandBoneName(eBone_Thumb0, true) == "finger_thumb_0_r");
	CHECK(HandBoneName(eBone_Aux_Thumb, false) == "finger_thumb_l_aux");
	CHECK(HandBoneName(eBone_Root, false) == "Root");
}

TEST_CASE("Compressed_round_trip")
{
	std::vector<vr::VRBoneTransform_t> hand = TestHand();
	std::vector<uint8_t> buffer(kCompressedBonesSize);
	CHECK(kCompressedBonesSize == 1004);
	CHECK(CompressBones(VRSkeletalTransformSpace_Model, vr::VRSkeletalMotionRange_WithController, hand.data(), buffer.data(), (uint32_t)buffer.size()) == 1004);
	CHECK(std::string((const char*)buffer.data(), 4) == "OCSK");

	vr::VRBoneTransform_t out[31];
	OOVR_EVRSkeletalTransformSpace stored = VRSkeletalTransformSpace_Parent;
	CHECK(DecompressBones(buffer.data(), (uint32_t)buffer.size(), VRSkeletalTransformSpace_Model, out, &stored) == vr::VRInputError_None);
	CHECK(stored == VRSkeletalTransformSpace_Model);
	CheckSame(hand, out);
}

TEST_CASE("Compressed_space_conversion")
{
	// Parent space in, model space out: as the conversion OpenComposite's bone data uses
	std::vector<vr::VRBoneTransform_t> parent = TestHand();
	std::vector<vr::VRBoneTransform_t> model = parent;
	BaseInput::ParentSpaceSkeletonToModelSpace(model.data());

	std::vector<uint8_t> buffer(kCompressedBonesSize);
	CompressBones(VRSkeletalTransformSpace_Parent, vr::VRSkeletalMotionRange_WithoutController, parent.data(), buffer.data(), (uint32_t)buffer.size());
	vr::VRBoneTransform_t out[31];
	CHECK(DecompressBones(buffer.data(), (uint32_t)buffer.size(), VRSkeletalTransformSpace_Model, out) == vr::VRInputError_None);
	CheckSame(model, out);

	// And back
	CompressBones(VRSkeletalTransformSpace_Model, vr::VRSkeletalMotionRange_WithoutController, model.data(), buffer.data(), (uint32_t)buffer.size());
	CHECK(DecompressBones(buffer.data(), (uint32_t)buffer.size(), VRSkeletalTransformSpace_Parent, out) == vr::VRInputError_None);
	CheckSame(parent, out);

	// No space asked for: as stored
	OOVR_EVRSkeletalTransformSpace stored = VRSkeletalTransformSpace_Parent;
	CHECK(DecompressBones(buffer.data(), (uint32_t)buffer.size(), std::nullopt, out, &stored) == vr::VRInputError_None);
	CHECK(stored == VRSkeletalTransformSpace_Model);
	CheckSame(model, out);
}

TEST_CASE("Decompress_rejects_garbage")
{
	std::vector<vr::VRBoneTransform_t> hand = TestHand();
	std::vector<uint8_t> buffer(kCompressedBonesSize);
	CompressBones(VRSkeletalTransformSpace_Model, vr::VRSkeletalMotionRange_WithController, hand.data(), buffer.data(), (uint32_t)buffer.size());
	vr::VRBoneTransform_t out[31];

	std::vector<uint8_t> wrongMagic = buffer;
	wrongMagic[0] = 'X';
	CHECK(DecompressBones(wrongMagic.data(), (uint32_t)wrongMagic.size(), std::nullopt, out) == vr::VRInputError_InvalidCompressedData);
	std::vector<uint8_t> wrongVersion = buffer;
	wrongVersion[4] = 2;
	CHECK(DecompressBones(wrongVersion.data(), (uint32_t)wrongVersion.size(), std::nullopt, out) == vr::VRInputError_InvalidCompressedData);
	CHECK(DecompressBones(buffer.data(), (uint32_t)buffer.size() - 1, std::nullopt, out) == vr::VRInputError_InvalidCompressedData);
	std::vector<uint8_t> garbage(kCompressedBonesSize, 0xAB);
	CHECK(DecompressBones(garbage.data(), (uint32_t)garbage.size(), std::nullopt, out) == vr::VRInputError_InvalidCompressedData);
	CHECK(DecompressBones(nullptr, 0, std::nullopt, out) == vr::VRInputError_InvalidParam);
}

TEST_CASE("Compressed_buffer_too_small_reports_size")
{
	std::vector<vr::VRBoneTransform_t> hand = TestHand();
	uint8_t tooSmall[100] = {};
	CHECK(CompressBones(VRSkeletalTransformSpace_Model, vr::VRSkeletalMotionRange_WithController, hand.data(), tooSmall, sizeof(tooSmall)) == 1004);
	CHECK(tooSmall[0] == 0); // nothing written
	CHECK(CompressBones(VRSkeletalTransformSpace_Model, vr::VRSkeletalMotionRange_WithController, hand.data(), nullptr, 0) == 1004);
}
