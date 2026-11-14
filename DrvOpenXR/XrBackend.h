//
// Created by ZNix on 25/10/2020.
//

#pragma once

#include <chrono>

#include "XrDriverPrivate.h"

#include "XrController.h"
#include "XrHMD.h"

#include <atomic>
#include <memory>
#include <vector>
#include <mutex>

class XrGenericTracker;

class XrBackend : public IBackend {
public:
	DECLARE_BACKEND_FUNCS(virtual, override)

	XrBackend(bool useVulkanTmpGfx, bool useD3D11TmpGfx);
	~XrBackend() override;

	/**
	 * The current state of the OpenXR session, or XR_SESSION_STATE_UNKNOWN if there is no session.
	 */
	XrSessionState sessionState = XR_SESSION_STATE_UNKNOWN;

	XrSessionState GetSessionState();

	/**
	 * Whether the session is active or not. This cannot be determined from just the session state, since
	 * we're allowed to send frames after calling xrBeginSession but before the event comes through. Same
	 * with ending the session - we're allowed to submit frames after we receive the stopping event, until
	 * we call xrEndSession.
	 */
	std::atomic<bool> sessionActive{ false };
	std::atomic<bool> sessionLost{ false };
	std::atomic<bool> exitExpected{ false };
	// The app's graphics device couldn't be bound: the session stays on the temporary device
	std::atomic<bool> appGraphicsUnusable{ false };

	/**
	 * To be called after xrCreateSession. Should only be used by DrvOpenXR.
	 */
	void OnSessionCreated();

	/**
	 * The session is gone or going (LOSS_PENDING, an EXITING OpenComposite didn't ask for, or a session restart
	 * that failed): VREvent_Quit is queued once, and no more frames are begun. There's no reconnecting.
	 */
	void OnSessionLost();
	bool IsSessionLost() const { return sessionLost; }

	/** Called before OpenComposite asks the runtime to end the session itself, so its EXITING isn't a loss */
	void ExpectSessionExit() { exitExpected = true; }

	/** False if neither D3D11 nor Vulkan could start the session (CreateOpenXRBackend then gives up) */
	bool HasTemporaryGraphics() const { return temporaryGraphics != nullptr; }

	void PrepareForSessionShutdown();

	const void* GetCurrentGraphicsBinding();

	void RegisterOverlayCompositor(std::shared_ptr<Compositor> compositor);
	void UnregisterOverlayCompositor(std::shared_ptr<Compositor> compositor);

	/**
	 * Restarts the session to allow for inputs to be attached to the session, if necessary.
	 * To be called from BaseInput whenever it's attempting to attach the game actions.
	 * Restarting the session should only be necessary if we've already created the infoSet.
	 */
	static void MaybeRestartForInputs();

#ifdef SUPPORT_VK
	static void VkGetPhysicalDevice(VkInstance instance, VkPhysicalDevice* out);
#endif

private:
	std::shared_ptr<XrHMD> hmd = std::make_shared<XrHMD>();
	std::shared_ptr<XrController> hand_left;
	std::shared_ptr<XrController> hand_right;

	std::vector<std::shared_ptr<XrGenericTracker>> generic_trackers;
	std::shared_mutex generic_trackers_mutex;

	// False if the texture can't be used (logged): no session switch then, or the switch was undone
	bool CheckOrInitCompositors(const vr::Texture_t* tex);
	std::unique_ptr<Compositor> compositors[XruEyeCount];
	std::unique_ptr<Compositor> skybox_compositor;
	std::vector<std::shared_ptr<Compositor>> overlay_compositors;

	/**
	 * Updates the current interaction profile in use according to the runtime.
	 * This will set the XrHMD's interaction profile, as well as create the XrControllers
	 * and set their interaction profiles accordingly.
	 * This allows for games to retrieve correct per-controller OpenVR properties that they request.
	 * Called from PumpEvents on an INTERACTION_PROFILE_CHANGED event.
	 */
	void UpdateInteractionProfile();

	/**
	 * Queries xdevs from MNDX_xdev_space, if the runtime supports it. Ignores HMD & Controllers.
	 * These "Generic Trackers" act like an HTC vive tracker, but support no bindings.
	 */
	void CreateGenericTrackers();

	/**
	 * Attempts to force the runtime to expose an interaction profile
	 * (i.e., send an INTERACTION_PROFILE_CHANGED event).
	 * If this is called before the game's actions have been attached to the session by BaseInput
	 * (which is the case for essentially all legacy input games), the session will have to be restarted
	 * to attach the actual inputs. BaseInput handles this when attaching its inputs.
	 */
	void QueryForInteractionProfile();
	void CreateInfoSet();
	void BindInfoSet();

	// Whether we've restarted the session to use the application's rendering API yet
	std::atomic<bool> usingApplicationGraphicsAPI{ false };

	// The views for the two main eye layers
	XrCompositionLayerProjectionView projectionViews[XruEyeCount];

	// Have we started rendering a frame yet? If not, calling xrEndFrame would result in an error
	bool renderingFrame = false;

	// A frame xrWaitFrame gave whose xrBeginFrame failed: the next frame begins it again instead of waiting, as a
	// second xrWaitFrame blocks until xrBeginFrame is called. Repeated failures lose the session.
	bool frameAwaitingBegin = false;
	// Consecutive frames whose xrWaitFrame or xrBeginFrame failed
	std::atomic<int> frameCallFailures{ 0 };
	// Drops the frame, waits about a frame and, if the session is lost or five frames failed in a row, loses the session
	void OnFrameCallFailed(XrResult res);

	// Were we supposed to start rendering a frame, but couldn't since we were on the
	// early (pre switch to application graphics instance) OpenXR session?
	bool deferredRenderingStart = false;

	// Keep track of if eye textures have been submitted and if we need to create a projection layer for them
	bool submittedEyeTextures = false;

	// If the app is using PostPresentHandoff then we need to delay when we submit frame data through xrEndFrame
	// until after all frame and layer data has been submitted and PostPresentHandoff is called. Otherwise we
	// might miss overlay elements for GUI or HUDs
	bool postPresentStatus = false;

	// Number of frames rendered for use in frame timing data
	uint32_t nFrameIndex = 0;

	double frameSubmitTimeUs = 0.0;

	// Measured frame timing for GetFrameTiming, taken around xrWaitFrame in WaitForTrackingData. Games such as
	// Half-Life: Alyx drive their dynamic resolution from these values, so constants freeze it at one level.
	// busy = time from one xrWaitFrame return to the next xrWaitFrame call (the app's render and submit), smoothed;
	// interval = time between xrWaitFrame calls, per frame.
	std::chrono::steady_clock::time_point lastWaitCall{};
	float measuredIntervalMs = 0.0f, measuredBusyMs = 0.0f, measuredWaitMs = 0.0f, displayPeriodMs = 0.0f;
	uint32_t measuredDropped = 0;
	XrTime lastPredictedDisplayTime = 0;
	void RecordFrameTiming(std::chrono::steady_clock::time_point callTime, std::chrono::steady_clock::time_point returnTime,
	    XrTime predictedDisplayTime, XrDuration predictedDisplayPeriod);
	// Starts measuring again from the next frame
	void RestartFrameTiming()
	{
		lastWaitCall = {};
		lastPredictedDisplayTime = 0;
		measuredDropped = 0;
	}

	// Action set and action used for querying for the interaction profile
	inline static XrActionSet infoSet = XR_NULL_HANDLE;
	XrAction infoAction = XR_NULL_HANDLE;
	XrPath subactionPaths[2] = { XR_NULL_PATH, XR_NULL_PATH };

	// Abstract class for holding a graphics binding.
	class BindingBase {
	public:
		virtual const void* asVoid() = 0;
		virtual ~BindingBase() = default;
	};

	// A wrapper around a graphics binding type, subclassing BindingBase
	// It is templated to allow storing any of the possible graphics bindings and getting a void* to it,
	// as necessary for xrCreateSession (in DrvOpenXR::SetupSession)
	template <typename T>
	class BindingWrapper : public BindingBase {
		const T data;

	public:
		BindingWrapper(T data)
		    : data(data) {}
		~BindingWrapper() override
		{
#if defined(SUPPORT_GL) && !defined(_WIN32)
			if constexpr (std::is_same_v<T, struct XrGraphicsBindingOpenGLXlibKHR>)
				glXDestroyContext(data.xDisplay, data.glxContext);
#endif
		}
		const void* asVoid() override { return &data; }
	};

	// The current graphics binding, used for restarting the session
	inline static std::unique_ptr<BindingBase> graphicsBinding = nullptr;
	static std::unique_ptr<class TemporaryGraphics> temporaryGraphics;
};
