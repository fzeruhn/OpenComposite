#include "stdafx.h"

#include "ProgramEvents.h"

#include "Misc/SharedStore.h"

#include <atomic>
#include <cstring>

static_assert(sizeof(vr::VREvent_Data_t) >= 24, "the data crossing between programs is its first 24 bytes");

namespace {
std::atomic<OcLocalEventSink> localSink{ nullptr };
}

void OcSetLocalEventSink(OcLocalEventSink sink)
{
	localSink = sink;
}

std::array<uint64_t, 4> OcPackEvent(vr::TrackedDeviceIndex_t device, const vr::VREvent_Data_t& data)
{
	std::array<uint64_t, 4> packed{};
	memcpy(packed.data(), &data, 24);
	packed[3] = device;
	return packed;
}

vr::VREvent_t OcUnpackEvent(uint32_t eventType, const uint64_t (&packed)[4])
{
	vr::VREvent_t e{};
	e.eventType = eventType;
	e.trackedDeviceIndex = (vr::TrackedDeviceIndex_t)packed[3];
	memcpy(&e.data, packed, 24);
	return e;
}

void OcPostEvent(vr::EVREventType type, vr::TrackedDeviceIndex_t device, const vr::VREvent_Data_t& data, bool alsoToOtherPrograms)
{
	if (OcLocalEventSink sink = localSink.load()) {
		vr::VREvent_t e{};
		e.eventType = type;
		e.trackedDeviceIndex = device;
		e.data = data;
		sink(e);
	}
	if (alsoToOtherPrograms)
		SharedStore::Get().PostEvent((uint32_t)type, OcPackEvent(device, data));
}
