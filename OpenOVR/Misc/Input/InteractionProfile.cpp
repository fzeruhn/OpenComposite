//
// Created by ZNix on 27/02/2021.
//

#include "stdafx.h"

#include <algorithm>
#include <cstring>
#include <set>
#include <utility>

#include "HolographicInteractionProfile.h"
#include "IndexControllerInteractionProfile.h"
#include "InteractionProfile.h"
#include "KhrSimpleInteractionProfile.h"
#include "OculusInteractionProfile.h"
#include "ReverbG2InteractionProfile.h"
#include "ViveInteractionProfile.h"
#include "ViveTrackerInteractionProfile.h"

#include "OculusHandPoses.h"

#include "Reimpl/BaseInput.h"
#include "generated/static_bases.gen.h"

std::string InteractionProfile::TranslateAction(const std::string& inputPath) const
{
	if (!pathTranslationMap.empty() && !IsInputPathValid(inputPath)) {
		std::string ret = inputPath;
		for (auto& [key, val] : pathTranslationMap) {
			size_t loc = ret.find(key);
			if (loc != std::string::npos) {
				// translate action!
				ret = ret.substr(0, loc) + val + ret.substr(loc + key.size());
			}
		}
		OOVR_LOGF("Translated path %s to %s for profile %s", inputPath.c_str(), ret.c_str(), GetPath().c_str());
		return ret;
	}
	// either this path is already valid or it's invalid and not translatable
	return inputPath;
}

std::vector<std::pair<std::string, std::string>> InteractionProfile::RawComponents() const
{
	// Every /user/hand/<hand>/input/<source>/<component> path: a parent (/input/thumbstick) or a pose isn't a raw input
	static const std::set<std::string> components = { "click", "touch", "value", "force", "x", "y" };
	std::vector<std::pair<std::string, std::string>> raw;
	for (const std::string& path : validInputPaths) {
		size_t input = path.find("/input/");
		if (input == std::string::npos || !path.starts_with("/user/hand/"))
			continue;
		size_t sourceStart = input + strlen("/input/");
		size_t slash = path.find('/', sourceStart);
		if (slash == std::string::npos || path.find('/', slash + 1) != std::string::npos)
			continue;
		std::string source = path.substr(sourceStart, slash - sourceStart);
		std::string component = path.substr(slash + 1);
		if (!components.count(component))
			continue;
		auto renamed = rawSourceNames.find(source);
		const std::string& steamvrSource = renamed == rawSourceNames.end() ? source : renamed->second;
		raw.emplace_back(path, path.substr(0, sourceStart) + steamvrSource + "/" + component);
	}
	std::sort(raw.begin(), raw.end());
	return raw;
}

const std::unordered_set<std::string>& InteractionProfile::GetValidInputPaths() const
{
	return validInputPaths;
}

bool InteractionProfile::IsInputPathValid(const std::string& inputPath) const
{
	return validInputPaths.find(inputPath) != validInputPaths.end();
}

void InteractionProfile::AddLegacyBindings(const LegacyControllerActions& ctrl, std::vector<XrActionSuggestedBinding>& bindings) const
{
	auto create = [&](XrAction action, const char* path) {
		// Not supported on this controller?
		if (path == nullptr)
			return;

		// Bind the action to a suggested binding
		std::string realPath = ctrl.handPath + "/" + path;
		if (!IsInputPathValid(realPath)) {
			// Only if an input profile gets its paths wrong: that binding is left out
			OOVR_LOGF("Found legacy input path %s, not supported by profile", realPath.c_str());
			return;
		}

		// The action must have been defined (it isn't if OpenXR couldn't make it): that binding is left out
		if (action == XR_NULL_HANDLE)
			return;

		XrActionSuggestedBinding binding = {};
		binding.action = action;

		if (XR_FAILED(xrStringToPath(xr_instance, realPath.c_str(), &binding.binding)))
			return;

		bindings.push_back(binding);
	};

	const LegacyBindings* paths = GetLegacyBindings(ctrl.handPath);

	create(ctrl.system, paths->system);
	create(ctrl.menu, paths->menu);
	create(ctrl.menuTouch, paths->menuTouch);
	create(ctrl.btnA, paths->btnA);
	create(ctrl.btnATouch, paths->btnATouch);
	create(ctrl.trackpadX, paths->trackpadX);
	create(ctrl.trackpadY, paths->trackpadY);
	create(ctrl.trackpadTouch, paths->trackpadTouch);
	create(ctrl.trackpadForce, paths->trackpadForce);
	create(ctrl.stickX, paths->stickX);
	create(ctrl.stickY, paths->stickY);
	create(ctrl.stickBtn, paths->stickBtn);
	create(ctrl.stickBtnTouch, paths->stickBtnTouch);
	create(ctrl.trigger, paths->trigger);
	create(ctrl.triggerTouch, paths->triggerTouch);
	create(ctrl.triggerClick, paths->triggerClick);
	create(ctrl.grip, paths->grip);
	create(ctrl.gripClick, paths->grip);
	create(ctrl.haptic, paths->haptic);
	create(ctrl.gripPoseAction, paths->gripPoseAction);
	create(ctrl.aimPoseAction, paths->aimPoseAction);
}

const InteractionProfile::ProfileList& InteractionProfile::GetProfileList()
{
	static std::vector<std::unique_ptr<InteractionProfile>> profiles;
	if (profiles.empty()) {
		if (xr_ext->G2Controller_Available())
			profiles.emplace_back(std::make_unique<ReverbG2InteractionProfile>());

		profiles.emplace_back(std::make_unique<HolographicInteractionProfile>());
		profiles.emplace_back(std::make_unique<IndexControllerInteractionProfile>());
		profiles.emplace_back(std::make_unique<ViveWandInteractionProfile>());
		profiles.emplace_back(std::make_unique<OculusTouchInteractionProfile>());
		profiles.emplace_back(std::make_unique<KhrSimpleInteractionProfile>());
		profiles.emplace_back(std::make_unique<ViveTrackerInteractionProfile>());
	}
	return profiles;
}

InteractionProfile* InteractionProfile::GetProfileByPath(const string& name)
{
	static std::map<std::string, InteractionProfile*> byPath;
	if (byPath.empty()) {
		for (const std::unique_ptr<InteractionProfile>& profile : GetProfileList()) {
			byPath[profile->GetPath()] = profile.get();
		}
	}
	if (!byPath.contains(name)) {
		OOVR_LOGF("Could not find interaction profile '%s'", name.c_str());
		return nullptr;
	}
	return byPath.at(name);
}

glm::mat4 InteractionProfile::GetGripToSteamVRTransform(ITrackedDevice::TrackedDeviceType hand) const
{
	if (hand == ITrackedDevice::TrackedDeviceType::HAND_LEFT) {
		return leftHandGripTransform;
	} else if (hand == ITrackedDevice::TrackedDeviceType::HAND_RIGHT) {
		return rightHandGripTransform;
	}
	return glm::identity<glm::mat4>();
}

std::optional<glm::mat4> InteractionProfile::GetComponentTransform(ITrackedDevice::TrackedDeviceType hand, const std::string& name) const
{
	std::unordered_map<std::string, glm::mat4> transforms = hand == ITrackedDevice::HAND_RIGHT ? rightComponentTransforms : leftComponentTransforms;
	const auto iter = transforms.find(name);
	if (iter == transforms.end())
		return {};
	else
		return iter->second;
}

std::optional<std::array<vr::VRBoneTransform_t, 31>> InteractionProfile::GetSkeletalReferencePose(ITrackedDevice::TrackedDeviceType hand, int pose) const
{
	auto poses = hand == ITrackedDevice::HAND_LEFT ? leftHandPoses : rightHandPoses;

	// Fall back to oculus poses, which should be close enough for most controllers
	if (poses.empty()) {
		OOVR_LOG_ONCE("WARNING: No reference poses defined for interaction profile, using fallback.");
		if (hand == ITrackedDevice::HAND_LEFT) {
			poses[VRSkeletalReferencePose_BindPose] = oculus::leftBindPose;
			poses[VRSkeletalReferencePose_OpenHand] = oculus::leftOpenHandPose;
			poses[VRSkeletalReferencePose_Fist] = oculus::leftFistPose;
			poses[VRSkeletalReferencePose_GripLimit] = oculus::leftGripLimitPose;
		} else {
			poses[VRSkeletalReferencePose_BindPose] = oculus::rightBindPose;
			poses[VRSkeletalReferencePose_OpenHand] = oculus::rightOpenHandPose;
			poses[VRSkeletalReferencePose_Fist] = oculus::rightFistPose;
			poses[VRSkeletalReferencePose_GripLimit] = oculus::rightGripLimitPose;
		}
	}

	const auto iter = poses.find(pose);
	if (iter == poses.end())
		return {};
	else
		return iter->second;
}
