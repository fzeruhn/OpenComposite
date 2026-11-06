#pragma once
#include "BaseCommon.h"

// The generated wrapper passes these as the OOVR_ types, and its out parameter as VRNotificationId.
typedef int OOVR_EVRNotificationType;
typedef int OOVR_EVRNotificationStyle;
typedef void OOVR_NotificationBitmap_t;
typedef uint32_t OOVR_VRNotificationId;
typedef uint32_t VRNotificationId;

// SteamVR's desktop-style notifications, which there's nowhere to show: they're accepted and
// dropped, so apps that ask for the interface (Batman: Arkham VR) carry on.
class BaseNotifications {
public:
	vr::EVRNotificationError CreateNotification(vr::VROverlayHandle_t ulOverlayHandle, uint64_t ulUserValue,
	    OOVR_EVRNotificationType type, const char* pchText, OOVR_EVRNotificationStyle style,
	    const OOVR_NotificationBitmap_t* pImage, VRNotificationId* pNotificationId);

	vr::EVRNotificationError RemoveNotification(OOVR_VRNotificationId notificationId);
};
