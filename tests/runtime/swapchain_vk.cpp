// The stand-in runtime's Vulkan sessions (XR_KHR_vulkan_enable and XR_KHR_vulkan_enable2): swapchain images made on
// the app's device, read back through a copy on the app's queue. Vulkan's functions come from the loader the app has
// loaded (vulkan-1.dll), looked up at run time.

#include "runtime_internal.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>

namespace {

PFN_vkGetInstanceProcAddr LoaderGetInstanceProcAddr()
{
	HMODULE module = GetModuleHandleA("vulkan-1.dll");
	if (!module)
		module = LoadLibraryA("vulkan-1.dll");
	return module ? (PFN_vkGetInstanceProcAddr)GetProcAddress(module, "vkGetInstanceProcAddr") : nullptr;
}

// The functions of the session's device
struct Fns {
	VkDevice device = VK_NULL_HANDLE;
	PFN_vkGetPhysicalDeviceMemoryProperties GetPhysicalDeviceMemoryProperties = nullptr;
#define OC_VK_DEVICE_FNS(X)                                                                                         \
	X(GetDeviceQueue) X(CreateImage) X(DestroyImage) X(GetImageMemoryRequirements) X(AllocateMemory) X(FreeMemory) \
	X(BindImageMemory) X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements) X(BindBufferMemory)          \
	X(MapMemory) X(UnmapMemory) X(CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers)                \
	X(BeginCommandBuffer) X(EndCommandBuffer) X(CmdPipelineBarrier) X(CmdCopyImageToBuffer) X(QueueSubmit)          \
	X(CreateFence) X(DestroyFence) X(WaitForFences)
#define OC_VK_DECLARE(name) PFN_vk##name name = nullptr;
	OC_VK_DEVICE_FNS(OC_VK_DECLARE)
#undef OC_VK_DECLARE
};
Fns g_vk;

// The instance of the last xrCreateVulkanInstanceKHR or xrGetVulkanGraphicsDevice2KHR, for xrCreateVulkanDeviceKHR
VkInstance g_lastInstance = VK_NULL_HANDLE;

// The physical device xrGetVulkanGraphicsDevice(2)KHR named for each VkInstance: a session's binding must use it
std::map<VkInstance, VkPhysicalDevice> g_named;

bool LoadDeviceFns(VkInstance instance, VkDevice device)
{
	PFN_vkGetInstanceProcAddr gipa = LoaderGetInstanceProcAddr();
	if (!gipa)
		return false;
	auto gdpa = (PFN_vkGetDeviceProcAddr)gipa(instance, "vkGetDeviceProcAddr");
	g_vk = Fns{};
	g_vk.GetPhysicalDeviceMemoryProperties = (PFN_vkGetPhysicalDeviceMemoryProperties)gipa(instance, "vkGetPhysicalDeviceMemoryProperties");
	if (!gdpa || !g_vk.GetPhysicalDeviceMemoryProperties)
		return false;
	bool ok = true;
#define OC_VK_LOAD(name)                                          \
	g_vk.name = (PFN_vk##name)gdpa(device, "vk" #name);           \
	ok = ok && g_vk.name != nullptr;
	OC_VK_DEVICE_FNS(OC_VK_LOAD)
#undef OC_VK_LOAD
	g_vk.device = ok ? device : VK_NULL_HANDLE;
	return ok;
}

// The physical device the runtime wants: the first whose deviceUUID is OC_TEST_RUNTIME_GPU_UUID (hex, dashes
// allowed) when that is set, else the first. The UUID comes from vkGetPhysicalDeviceProperties2: with the variable
// set, the app's instance must be Vulkan 1.1 or have VK_KHR_get_physical_device_properties2 enabled.
VkPhysicalDevice ChoosePhysicalDevice(VkInstance instance)
{
	PFN_vkGetInstanceProcAddr gipa = LoaderGetInstanceProcAddr();
	if (!gipa || !instance)
		return VK_NULL_HANDLE;
	auto enumerate = (PFN_vkEnumeratePhysicalDevices)gipa(instance, "vkEnumeratePhysicalDevices");
	if (!enumerate)
		return VK_NULL_HANDLE;
	uint32_t count = 0;
	if (enumerate(instance, &count, nullptr) != VK_SUCCESS || count == 0)
		return VK_NULL_HANDLE;
	std::vector<VkPhysicalDevice> devices(count);
	if (enumerate(instance, &count, devices.data()) < 0 || count == 0)
		return VK_NULL_HANDLE;
	const char* want = std::getenv("OC_TEST_RUNTIME_GPU_UUID");
	auto props2 = (PFN_vkGetPhysicalDeviceProperties2)gipa(instance, "vkGetPhysicalDeviceProperties2");
	if (!props2)
		props2 = (PFN_vkGetPhysicalDeviceProperties2)gipa(instance, "vkGetPhysicalDeviceProperties2KHR");
	if (want && *want && props2) {
		std::string hex;
		for (const char* c = want; *c; c++)
			if (std::isxdigit((unsigned char)*c))
				hex += (char)std::tolower((unsigned char)*c);
		for (uint32_t i = 0; i < count; i++) {
			VkPhysicalDeviceIDProperties id{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES };
			VkPhysicalDeviceProperties2 p{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
			p.pNext = &id;
			props2(devices[i], &p);
			char buf[3];
			std::string uuid;
			for (uint8_t b : id.deviceUUID) {
				std::snprintf(buf, sizeof(buf), "%02x", b);
				uuid += buf;
			}
			if (uuid == hex)
				return devices[i];
		}
	}
	return devices[0];
}

// --- XR_KHR_vulkan_enable and XR_KHR_vulkan_enable2 ---

XrResult XRAPI_CALL GetVulkanGraphicsRequirements(XrInstance, XrSystemId, XrGraphicsRequirementsVulkanKHR* reqs)
{
	XrResult injected;
	if (ScriptFails("xrGetVulkanGraphicsRequirementsKHR", &injected))
		return injected;
	{
		std::lock_guard<std::recursive_mutex> g(St().lock);
		St().requirementsQueried.insert(RtApi::Vulkan);
	}
	reqs->minApiVersionSupported = XR_MAKE_VERSION(1, 0, 0);
	reqs->maxApiVersionSupported = XR_MAKE_VERSION(1, 4, 0);
	return XR_SUCCESS;
}

// No instance or device extensions are needed: the images stay on the app's device
XrResult XRAPI_CALL GetVulkanExtensions(XrInstance, XrSystemId, uint32_t capacity, uint32_t* count, char* buffer)
{
	*count = 1;
	if (capacity == 0)
		return XR_SUCCESS;
	buffer[0] = 0;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL GetVulkanGraphicsDevice(XrInstance, XrSystemId, VkInstance vkInstance, VkPhysicalDevice* out)
{
	*out = ChoosePhysicalDevice(vkInstance);
	if (*out)
		g_named[vkInstance] = *out;
	return *out ? XR_SUCCESS : XR_ERROR_RUNTIME_FAILURE;
}

XrResult XRAPI_CALL GetVulkanGraphicsDevice2(XrInstance, const XrVulkanGraphicsDeviceGetInfoKHR* info, VkPhysicalDevice* out)
{
	g_lastInstance = info->vulkanInstance;
	*out = ChoosePhysicalDevice(info->vulkanInstance);
	if (*out)
		g_named[info->vulkanInstance] = *out;
	return *out ? XR_SUCCESS : XR_ERROR_RUNTIME_FAILURE;
}

XrResult XRAPI_CALL CreateVulkanInstance(XrInstance, const XrVulkanInstanceCreateInfoKHR* info, VkInstance* out, VkResult* vkResult)
{
	if (!info->pfnGetInstanceProcAddr || !info->vulkanCreateInfo)
		return XR_ERROR_VALIDATION_FAILURE;
	auto create = (PFN_vkCreateInstance)info->pfnGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance");
	if (!create)
		return XR_ERROR_RUNTIME_FAILURE;
	*vkResult = create(info->vulkanCreateInfo, info->vulkanAllocator, out);
	if (*vkResult == VK_SUCCESS)
		g_lastInstance = *out;
	return XR_SUCCESS;
}

XrResult XRAPI_CALL CreateVulkanDevice(XrInstance, const XrVulkanDeviceCreateInfoKHR* info, VkDevice* out, VkResult* vkResult)
{
	if (!info->pfnGetInstanceProcAddr || !info->vulkanCreateInfo || !info->vulkanPhysicalDevice || !g_lastInstance)
		return XR_ERROR_VALIDATION_FAILURE;
	auto create = (PFN_vkCreateDevice)info->pfnGetInstanceProcAddr(g_lastInstance, "vkCreateDevice");
	if (!create)
		return XR_ERROR_RUNTIME_FAILURE;
	*vkResult = create(info->vulkanPhysicalDevice, info->vulkanCreateInfo, info->vulkanAllocator, out);
	return XR_SUCCESS;
}

// --- the session ---

// Monado's checks: handles set (XR_ERROR_GRAPHICS_DEVICE_INVALID), the physical device the one
// xrGetVulkanGraphicsDevice(2)KHR named for the instance (XR_ERROR_VALIDATION_FAILURE, also when it wasn't asked), and
// here also a queue family and index the physical device has (XR_ERROR_GRAPHICS_DEVICE_INVALID)
XrResult Bind(const XrBaseInStructure* binding, RtSession* s)
{
	auto* b = (const XrGraphicsBindingVulkanKHR*)binding;
	if (!b->instance || !b->physicalDevice || !b->device)
		return XR_ERROR_GRAPHICS_DEVICE_INVALID;
	auto named = g_named.find(b->instance);
	if (named == g_named.end() || named->second != b->physicalDevice)
		return XR_ERROR_VALIDATION_FAILURE;
	PFN_vkGetInstanceProcAddr gipa = LoaderGetInstanceProcAddr();
	auto families = gipa ? (PFN_vkGetPhysicalDeviceQueueFamilyProperties)gipa(b->instance, "vkGetPhysicalDeviceQueueFamilyProperties") : nullptr;
	if (!families)
		return XR_ERROR_RUNTIME_FAILURE;
	uint32_t n = 0;
	families(b->physicalDevice, &n, nullptr);
	std::vector<VkQueueFamilyProperties> props(n);
	families(b->physicalDevice, &n, props.data());
	if (b->queueFamilyIndex >= n || b->queueIndex >= props[b->queueFamilyIndex].queueCount)
		return XR_ERROR_GRAPHICS_DEVICE_INVALID;
	if (!LoadDeviceFns(b->instance, b->device))
		return XR_ERROR_GRAPHICS_DEVICE_INVALID;
	s->vkInstance = b->instance;
	s->vkPhysicalDevice = b->physicalDevice;
	s->vkDevice = b->device;
	s->vkQueueFamily = b->queueFamilyIndex;
	s->vkQueueIndex = b->queueIndex;
	return XR_SUCCESS;
}

void Unbind(RtSession* s)
{
	s->vkDevice = VK_NULL_HANDLE;
	g_vk = Fns{};
}

const std::vector<int64_t>& Formats()
{
	static const std::vector<int64_t> formats = {
		VK_FORMAT_R8G8B8A8_SRGB,
		VK_FORMAT_R8G8B8A8_UNORM,
		VK_FORMAT_B8G8R8A8_SRGB,
		VK_FORMAT_B8G8R8A8_UNORM,
		VK_FORMAT_R16G16B16A16_SFLOAT,
		VK_FORMAT_A2B10G10R10_UNORM_PACK32,
		VK_FORMAT_D32_SFLOAT,
	};
	return formats;
}

const std::vector<int64_t>& DefaultFormats()
{
	static const std::vector<int64_t> formats = { VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_R8G8B8A8_UNORM,
		VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_B8G8R8A8_SRGB };
	return formats;
}

PixelLayout Layout(int64_t format)
{
	switch (format) {
	case VK_FORMAT_R8G8B8A8_SRGB:
	case VK_FORMAT_R8G8B8A8_UNORM:
		return PixelLayout::RGBA8;
	case VK_FORMAT_B8G8R8A8_SRGB:
	case VK_FORMAT_B8G8R8A8_UNORM:
		return PixelLayout::BGRA8;
	case VK_FORMAT_R16G16B16A16_SFLOAT:
		return PixelLayout::RGBA16F;
	case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
		return PixelLayout::RGB10A2; // R in the low bits, as DXGI's R10G10B10A2
	case VK_FORMAT_D32_SFLOAT:
	case VK_FORMAT_D16_UNORM:
	case VK_FORMAT_D24_UNORM_S8_UINT:
	case VK_FORMAT_D32_SFLOAT_S8_UINT:
		return PixelLayout::Depth;
	default:
		return PixelLayout::Unknown;
	}
}

int32_t MemoryType(RtSession* s, uint32_t bits, VkMemoryPropertyFlags want)
{
	VkPhysicalDeviceMemoryProperties props;
	g_vk.GetPhysicalDeviceMemoryProperties(s->vkPhysicalDevice, &props);
	for (uint32_t i = 0; i < props.memoryTypeCount; i++)
		if ((bits & (1u << i)) && (props.memoryTypes[i].propertyFlags & want) == want)
			return (int32_t)i;
	return -1;
}

// Records commands into a one-time command buffer and runs them on the app's queue, waiting at most 5 s; after a
// timeout (*timedOut) the pool and fence are left alive, the GPU may still use them
bool RunOnce(RtSession* s, const std::function<void(VkCommandBuffer)>& record, bool* timedOut = nullptr)
{
	if (timedOut)
		*timedOut = false;
	if (g_vk.device != s->vkDevice || !s->vkDevice)
		return false;
	VkQueue queue = VK_NULL_HANDLE;
	g_vk.GetDeviceQueue(s->vkDevice, s->vkQueueFamily, s->vkQueueIndex, &queue);
	if (!queue)
		return false;
	VkCommandPoolCreateInfo pci{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
	pci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	pci.queueFamilyIndex = s->vkQueueFamily;
	VkCommandPool pool = VK_NULL_HANDLE;
	if (g_vk.CreateCommandPool(s->vkDevice, &pci, nullptr, &pool) != VK_SUCCESS)
		return false;
	VkCommandBufferAllocateInfo ai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
	ai.commandPool = pool;
	ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	ai.commandBufferCount = 1;
	VkCommandBuffer cb = VK_NULL_HANDLE;
	VkFence fence = VK_NULL_HANDLE;
	VkFenceCreateInfo fci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
	bool ok = g_vk.AllocateCommandBuffers(s->vkDevice, &ai, &cb) == VK_SUCCESS
	    && g_vk.CreateFence(s->vkDevice, &fci, nullptr, &fence) == VK_SUCCESS;
	if (ok) {
		VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		ok = g_vk.BeginCommandBuffer(cb, &bi) == VK_SUCCESS;
	}
	if (ok) {
		record(cb);
		ok = g_vk.EndCommandBuffer(cb) == VK_SUCCESS;
	}
	if (ok) {
		VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
		si.commandBufferCount = 1;
		si.pCommandBuffers = &cb;
		ok = g_vk.QueueSubmit(queue, 1, &si, fence) == VK_SUCCESS;
		if (ok && g_vk.WaitForFences(s->vkDevice, 1, &fence, VK_TRUE, 5000000000ull) != VK_SUCCESS) {
			RtNoteReadbackTimeout();
			if (timedOut)
				*timedOut = true;
			return false;
		}
	}
	if (fence)
		g_vk.DestroyFence(s->vkDevice, fence, nullptr);
	g_vk.DestroyCommandPool(s->vkDevice, pool, nullptr); // frees the command buffer
	return ok;
}

void Barrier(VkCommandBuffer cb, VkImage image, VkImageAspectFlags aspect, uint32_t layer, uint32_t layers,
    VkImageLayout from, VkImageLayout to)
{
	VkImageMemoryBarrier b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
	b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
	b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
	b.oldLayout = from;
	b.newLayout = to;
	b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.image = image;
	b.subresourceRange = { aspect, 0, VK_REMAINING_MIP_LEVELS, layer, layers };
	g_vk.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
	    nullptr, 1, &b);
}

void Destroy(RtSession* s, RtSwapchain* sc)
{
	if (s && g_vk.device == s->vkDevice && s->vkDevice) {
		for (uint64_t image : sc->images)
			g_vk.DestroyImage(s->vkDevice, (VkImage)image, nullptr);
		for (uint64_t mem : sc->memory)
			g_vk.FreeMemory(s->vkDevice, (VkDeviceMemory)mem, nullptr);
	}
	sc->images.clear();
	sc->memory.clear();
}

XrResult Create(RtSession* s, const XrSwapchainCreateInfo* info, RtSwapchain* sc)
{
	if (!s->vkDevice || g_vk.device != s->vkDevice)
		return XR_ERROR_VALIDATION_FAILURE;
	if (std::find(Formats().begin(), Formats().end(), info->format) == Formats().end())
		return XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED;
	bool depth = Layout(info->format) == PixelLayout::Depth;
	uint32_t layers = (info->arraySize ? info->arraySize : 1) * (info->faceCount ? info->faceCount : 1);
	VkImageCreateInfo ici{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	ici.imageType = VK_IMAGE_TYPE_2D;
	ici.format = (VkFormat)info->format;
	ici.extent = { info->width, info->height, 1 };
	ici.mipLevels = info->mipCount ? info->mipCount : 1;
	ici.arrayLayers = layers;
	ici.samples = (VkSampleCountFlagBits)(info->sampleCount ? info->sampleCount : 1);
	ici.tiling = VK_IMAGE_TILING_OPTIMAL;
	ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
	    | (depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
	if (info->usageFlags & XR_SWAPCHAIN_USAGE_UNORDERED_ACCESS_BIT)
		ici.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
	if (info->usageFlags & XR_SWAPCHAIN_USAGE_MUTABLE_FORMAT_BIT)
		ici.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
	if (info->faceCount == 6)
		ici.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
	ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	sc->info = *info;
	for (int i = 0; i < 3; i++) {
		VkImage image = VK_NULL_HANDLE;
		if (g_vk.CreateImage(s->vkDevice, &ici, nullptr, &image) != VK_SUCCESS) {
			Destroy(s, sc);
			return XR_ERROR_RUNTIME_FAILURE;
		}
		sc->images.push_back((uint64_t)image);
		VkMemoryRequirements req;
		g_vk.GetImageMemoryRequirements(s->vkDevice, image, &req);
		int32_t type = MemoryType(s, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VkMemoryAllocateInfo mai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
		mai.allocationSize = req.size;
		mai.memoryTypeIndex = (uint32_t)type;
		VkDeviceMemory mem = VK_NULL_HANDLE;
		if (type < 0 || g_vk.AllocateMemory(s->vkDevice, &mai, nullptr, &mem) != VK_SUCCESS) {
			Destroy(s, sc);
			return XR_ERROR_RUNTIME_FAILURE;
		}
		sc->memory.push_back((uint64_t)mem);
		if (g_vk.BindImageMemory(s->vkDevice, image, mem, 0) != VK_SUCCESS) {
			Destroy(s, sc);
			return XR_ERROR_RUNTIME_FAILURE;
		}
	}
	// The spec's layout of an acquired image: colour (depth) attachment optimal
	VkImageAspectFlags aspect = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
	VkImageLayout layout = depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	bool ok = RunOnce(s, [&](VkCommandBuffer cb) {
		for (uint64_t image : sc->images)
			Barrier(cb, (VkImage)image, aspect, 0, VK_REMAINING_ARRAY_LAYERS, VK_IMAGE_LAYOUT_UNDEFINED, layout);
	});
	if (!ok) {
		Destroy(s, sc);
		return XR_ERROR_RUNTIME_FAILURE;
	}
	return XR_SUCCESS;
}

void Enumerate(const RtSwapchain* sc, XrSwapchainImageBaseHeader* images)
{
	auto* out = (XrSwapchainImageVulkanKHR*)images;
	for (size_t i = 0; i < sc->images.size(); i++)
		out[i].image = (VkImage)sc->images[i];
}

bool Read(RtSession* s, RtSwapchain* sc, uint32_t index, uint32_t arrayIndex, const std::vector<XrOffset2Di>& points,
    uint32_t bytes, std::vector<uint8_t>& out, std::string* why)
{
	*why = "the Vulkan readback failed";
	if (!s->vkDevice || g_vk.device != s->vkDevice || index >= sc->images.size() || points.empty())
		return false;
	if (sc->info.sampleCount > 1)
		return false; // a multisampled image isn't read back
	VkDevice dev = s->vkDevice;
	VkBufferCreateInfo bci{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	bci.size = (VkDeviceSize)bytes * points.size();
	bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceMemory mem = VK_NULL_HANDLE;
	bool ok = g_vk.CreateBuffer(dev, &bci, nullptr, &buffer) == VK_SUCCESS;
	if (ok) {
		VkMemoryRequirements req;
		g_vk.GetBufferMemoryRequirements(dev, buffer, &req);
		int32_t type = MemoryType(s, req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		VkMemoryAllocateInfo mai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
		mai.allocationSize = req.size;
		mai.memoryTypeIndex = (uint32_t)type;
		ok = type >= 0 && g_vk.AllocateMemory(dev, &mai, nullptr, &mem) == VK_SUCCESS
		    && g_vk.BindBufferMemory(dev, buffer, mem, 0) == VK_SUCCESS;
	}
	if (ok) {
		auto image = (VkImage)sc->images[index];
		std::vector<VkBufferImageCopy> regions;
		for (size_t i = 0; i < points.size(); i++) {
			VkBufferImageCopy r{};
			r.bufferOffset = (VkDeviceSize)bytes * i;
			r.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, arrayIndex, 1 };
			r.imageOffset = { std::min<int32_t>(std::max<int32_t>(points[i].x, 0), (int32_t)sc->info.width - 1),
				std::min<int32_t>(std::max<int32_t>(points[i].y, 0), (int32_t)sc->info.height - 1), 0 };
			r.imageExtent = { 1, 1, 1 };
			regions.push_back(r);
		}
		bool timedOut = false;
		ok = RunOnce(s, [&](VkCommandBuffer cb) {
			Barrier(cb, image, VK_IMAGE_ASPECT_COLOR_BIT, arrayIndex, 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
			g_vk.CmdCopyImageToBuffer(cb, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, (uint32_t)regions.size(), regions.data());
			Barrier(cb, image, VK_IMAGE_ASPECT_COLOR_BIT, arrayIndex, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		}, &timedOut);
		if (timedOut) {
			*why = "the Vulkan readback timed out"; // the buffer stays alive with the copy into it
			return false;
		}
	}
	if (ok) {
		void* data = nullptr;
		ok = g_vk.MapMemory(dev, mem, 0, bci.size, 0, &data) == VK_SUCCESS;
		if (ok) {
			out.assign((const uint8_t*)data, (const uint8_t*)data + bci.size);
			g_vk.UnmapMemory(dev, mem);
		}
	}
	if (buffer)
		g_vk.DestroyBuffer(dev, buffer, nullptr);
	if (mem)
		g_vk.FreeMemory(dev, mem, nullptr);
	return ok;
}

} // namespace

const RtGfx& RtVulkanGfx()
{
	static const RtGfx gfx = {
		RtApi::Vulkan, "vulkan", "XR_KHR_vulkan_enable", "XR_KHR_vulkan_enable2", XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR, XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR,
		Bind, Unbind, Formats, DefaultFormats, Create, Destroy, Enumerate, Layout, false, Read,
	};
	return gfx;
}

PFN_xrVoidFunction RtVulkanLookup(const char* name, const char** extension)
{
	struct Entry {
		const char* name;
		PFN_xrVoidFunction fn;
		const char* extension;
	};
	static const Entry entries[] = {
		{ "xrGetVulkanGraphicsRequirementsKHR", (PFN_xrVoidFunction)GetVulkanGraphicsRequirements, "XR_KHR_vulkan_enable" },
		{ "xrGetVulkanInstanceExtensionsKHR", (PFN_xrVoidFunction)GetVulkanExtensions, "XR_KHR_vulkan_enable" },
		{ "xrGetVulkanDeviceExtensionsKHR", (PFN_xrVoidFunction)GetVulkanExtensions, "XR_KHR_vulkan_enable" },
		{ "xrGetVulkanGraphicsDeviceKHR", (PFN_xrVoidFunction)GetVulkanGraphicsDevice, "XR_KHR_vulkan_enable" },
		{ "xrGetVulkanGraphicsRequirements2KHR", (PFN_xrVoidFunction)GetVulkanGraphicsRequirements, "XR_KHR_vulkan_enable2" },
		{ "xrCreateVulkanInstanceKHR", (PFN_xrVoidFunction)CreateVulkanInstance, "XR_KHR_vulkan_enable2" },
		{ "xrCreateVulkanDeviceKHR", (PFN_xrVoidFunction)CreateVulkanDevice, "XR_KHR_vulkan_enable2" },
		{ "xrGetVulkanGraphicsDevice2KHR", (PFN_xrVoidFunction)GetVulkanGraphicsDevice2, "XR_KHR_vulkan_enable2" },
	};
	for (const Entry& e : entries) {
		if (!std::strcmp(e.name, name)) {
			*extension = e.extension;
			return e.fn;
		}
	}
	return nullptr;
}
