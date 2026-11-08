#include "stdafx.h"
#define GENFILE
#include "BaseCommon.h"

GEN_INTERFACE("Notifications", "001")
GEN_INTERFACE("Notifications", "002")

#include "generated/GVRNotifications.gen.h"

// IVRNotifications_001 (SDK 0.9.x) named a notification's type and category with strings: dropped as today's are
vr::IVRNotifications_001::NotificationError_t CVRNotifications_001::CreateNotification(vr::VROverlayHandle_t ulOverlayHandle,
    uint64_t ulUserValue, const char* strType, const char* strText, const char* strCategory,
    const vr::IVRNotifications_001::NotificationBitmap* photo, vr::IVRNotifications_001::VRNotificationId* notificationId)
{
	VRNotificationId id = 0;
	vr::EVRNotificationError err = base->CreateNotification(ulOverlayHandle, ulUserValue, 0, strText, 0, nullptr, &id);
	if (notificationId)
		*notificationId = id;
	return err == vr::VRNotificationError_OK ? vr::IVRNotifications_001::k_ENotificationError_OK
	                                          : vr::IVRNotifications_001::k_ENotificationError_Fail;
}

vr::IVRNotifications_001::NotificationError_t CVRNotifications_001::DismissNotification(vr::IVRNotifications_001::VRNotificationId notificationId)
{
	base->RemoveNotification(notificationId);
	return vr::IVRNotifications_001::k_ENotificationError_OK;
}

uint32_t CVRNotifications_001::GetErrorString(vr::IVRNotifications_001::NotificationError_t error, char* pchBuffer, uint32_t unBufferSize)
{
	return base->GetErrorString((int)error, pchBuffer, unBufferSize);
}
