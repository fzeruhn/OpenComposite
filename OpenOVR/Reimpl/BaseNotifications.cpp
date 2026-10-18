#include "stdafx.h"
#define BASE_IMPL
#include "BaseNotifications.h"

vr::EVRNotificationError BaseNotifications::CreateNotification(vr::VROverlayHandle_t ulOverlayHandle, uint64_t ulUserValue,
    OOVR_EVRNotificationType type, const char* pchText, OOVR_EVRNotificationStyle style,
    const OOVR_NotificationBitmap_t* pImage, VRNotificationId* pNotificationId)
{
	static uint32_t next_id = 1;
	OOVR_LOGF("Dropping notification: %s", pchText ? pchText : "(no text)");
	if (pNotificationId)
		*pNotificationId = next_id++;
	return vr::VRNotificationError_OK;
}

vr::EVRNotificationError BaseNotifications::RemoveNotification(OOVR_VRNotificationId notificationId)
{
	return vr::VRNotificationError_OK;
}
