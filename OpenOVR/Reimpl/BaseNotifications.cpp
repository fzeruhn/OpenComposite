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

uint32_t BaseNotifications::GetErrorString(int error, char* pchBuffer, uint32_t unBufferSize)
{
	const char* name = error == 0 ? "k_ENotificationError_OK"
	    : error == 1             ? "k_ENotificationError_Fail"
	    : error == 2             ? "k_eNotificationError_InvalidParam"
	                             : "Unknown NotificationError_t";
	uint32_t needed = (uint32_t)strlen(name) + 1;
	if (pchBuffer && unBufferSize) {
		strncpy(pchBuffer, name, unBufferSize - 1);
		pchBuffer[unBufferSize - 1] = '\0';
	}
	return needed;
}
