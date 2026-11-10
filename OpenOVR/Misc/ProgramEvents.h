#pragma once

// Events from calls: queued for this program's IVRSystem, and for every other OpenComposite program of the
// user through the store (SharedStore's event ring). Other programs' events are taken off the ring in PollNextEvent.

#include "generated/interfaces/vrtypes.h"

#include <array>
#include <cstdint>

/** Queues the event for this program (once it has an IVRSystem) and, if alsoToOtherPrograms, for the others */
void OcPostEvent(vr::EVREventType type, vr::TrackedDeviceIndex_t device, const vr::VREvent_Data_t& data, bool alsoToOtherPrograms);

/** Where this program's events go: BaseSystem sets it while it lives, nullptr for nowhere */
using OcLocalEventSink = void (*)(const vr::VREvent_t& event);
void OcSetLocalEventSink(OcLocalEventSink sink);

/** An event as it crosses between programs: data[0..2] its data's first 24 bytes, data[3] the device; and back */
std::array<uint64_t, 4> OcPackEvent(vr::TrackedDeviceIndex_t device, const vr::VREvent_Data_t& data);
vr::VREvent_t OcUnpackEvent(uint32_t eventType, const uint64_t (&packed)[4]);
