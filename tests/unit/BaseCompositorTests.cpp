// doctest first: stdafx.h brings in Xlib on Linux, whose macros (None, Bool, ...) clash with doctest's names
#include "doctest.h"

#include "stdafx.h"

#include "Reimpl/BaseCompositor.h"

TEST_CASE("Vk_extensions_size_insufficient_answers_needed_size")
{
	// A buffer too small: the size needed, as SteamVR answers (a game asks again with a buffer that big)
	CHECK(BaseCompositor::VulkanExtensionsSize(XR_ERROR_SIZE_INSUFFICIENT, 87) == 87);
	// Filled, or only asked for its size: the size
	CHECK(BaseCompositor::VulkanExtensionsSize(XR_SUCCESS, 87) == 87);
	// Any other failure (no Vulkan in the runtime, say): 0
	CHECK(BaseCompositor::VulkanExtensionsSize(XR_ERROR_FUNCTION_UNSUPPORTED, 87) == 0);
	CHECK(BaseCompositor::VulkanExtensionsSize(XR_ERROR_VALIDATION_FAILURE, 87) == 0);
}
