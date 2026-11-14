// Vulkan for the frame cases: an instance, the first physical device and a device with one graphics queue, all
// through vulkan-1.dll looked up at run time (oc_conformance starts without it). Textures are filled from a staging
// buffer and left in VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, as OpenVR's Vulkan games submit them.

#include "GfxApi.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>

#include <functional>

namespace {

gfx::Texels TexelsOfVk(int64_t format)
{
	switch (format) {
	case VK_FORMAT_R8G8B8A8_SRGB:
	case VK_FORMAT_R8G8B8A8_UNORM:
		return gfx::Texels::RGBA8;
	case VK_FORMAT_B8G8R8A8_SRGB:
	case VK_FORMAT_B8G8R8A8_UNORM:
		return gfx::Texels::BGRA8;
	case VK_FORMAT_R16G16B16A16_SFLOAT:
		return gfx::Texels::RGBA16F;
	case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
		return gfx::Texels::RGB10A2;
	default:
		return gfx::Texels::Unknown;
	}
}

class VkGfx : public GfxApi {
public:
	PFN_vkGetInstanceProcAddr gipa = nullptr;
	PFN_vkGetDeviceProcAddr gdpa = nullptr;
	VkInstance instance = VK_NULL_HANDLE;
	VkPhysicalDevice physical = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	VkQueue queue = VK_NULL_HANDLE;
	uint32_t family = 0;
	VkPhysicalDeviceMemoryProperties memProps{};
	struct Made {
		VkImage image;
		VkDeviceMemory memory;
	};
	std::vector<Made> made;

	template <typename T>
	T I(const char* name) { return (T)gipa(instance, name); }
	template <typename T>
	T D(const char* name) { return (T)gdpa(device, name); }

	~VkGfx() override
	{
		if (device) {
			Flush();
			for (const Made& m : made) {
				D<PFN_vkDestroyImage>("vkDestroyImage")(device, m.image, nullptr);
				D<PFN_vkFreeMemory>("vkFreeMemory")(device, m.memory, nullptr);
			}
			D<PFN_vkDestroyDevice>("vkDestroyDevice")(device, nullptr);
		}
		if (instance)
			I<PFN_vkDestroyInstance>("vkDestroyInstance")(instance, nullptr);
	}

	bool Init(std::string* whyNot)
	{
		HMODULE module = LoadLibraryA("vulkan-1.dll");
		gipa = module ? (PFN_vkGetInstanceProcAddr)GetProcAddress(module, "vkGetInstanceProcAddr") : nullptr;
		if (!gipa) {
			*whyNot = "no Vulkan loader (vulkan-1.dll)";
			return false;
		}
		VkApplicationInfo app{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
		app.pApplicationName = "oc_conformance";
		app.apiVersion = VK_API_VERSION_1_1; // the stand-in may read deviceUUIDs (vkGetPhysicalDeviceProperties2)
		VkInstanceCreateInfo ici{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
		ici.pApplicationInfo = &app;
		VkResult r = ((PFN_vkCreateInstance)gipa(VK_NULL_HANDLE, "vkCreateInstance"))(&ici, nullptr, &instance);
		if (r != VK_SUCCESS) {
			instance = VK_NULL_HANDLE;
			*whyNot = "no Vulkan instance (" + std::to_string((int)r) + ")";
			return false;
		}
		uint32_t count = 1;
		r = I<PFN_vkEnumeratePhysicalDevices>("vkEnumeratePhysicalDevices")(instance, &count, &physical);
		if ((r != VK_SUCCESS && r != VK_INCOMPLETE) || count == 0 || !physical) {
			*whyNot = "no Vulkan physical device";
			return false;
		}
		uint32_t n = 0;
		auto families = I<PFN_vkGetPhysicalDeviceQueueFamilyProperties>("vkGetPhysicalDeviceQueueFamilyProperties");
		families(physical, &n, nullptr);
		std::vector<VkQueueFamilyProperties> props(n);
		families(physical, &n, props.data());
		for (family = 0; family < n && !(props[family].queueFlags & VK_QUEUE_GRAPHICS_BIT); family++) {
		}
		if (family == n) {
			*whyNot = "no Vulkan graphics queue";
			return false;
		}
		float priority = 1;
		VkDeviceQueueCreateInfo qci{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
		qci.queueFamilyIndex = family;
		qci.queueCount = 1;
		qci.pQueuePriorities = &priority;
		VkDeviceCreateInfo dci{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
		dci.queueCreateInfoCount = 1;
		dci.pQueueCreateInfos = &qci;
		r = I<PFN_vkCreateDevice>("vkCreateDevice")(physical, &dci, nullptr, &device);
		if (r != VK_SUCCESS) {
			device = VK_NULL_HANDLE;
			*whyNot = "no Vulkan device (" + std::to_string((int)r) + ")";
			return false;
		}
		gdpa = I<PFN_vkGetDeviceProcAddr>("vkGetDeviceProcAddr");
		D<PFN_vkGetDeviceQueue>("vkGetDeviceQueue")(device, family, 0, &queue);
		I<PFN_vkGetPhysicalDeviceMemoryProperties>("vkGetPhysicalDeviceMemoryProperties")(physical, &memProps);
		return true;
	}

	int32_t MemoryType(uint32_t bits, VkMemoryPropertyFlags want)
	{
		for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
			if ((bits & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & want) == want)
				return (int32_t)i;
		return -1;
	}

	bool Run(const std::function<void(VkCommandBuffer)>& record)
	{
		VkCommandPoolCreateInfo pci{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
		pci.queueFamilyIndex = family;
		VkCommandPool pool = VK_NULL_HANDLE;
		if (D<PFN_vkCreateCommandPool>("vkCreateCommandPool")(device, &pci, nullptr, &pool) != VK_SUCCESS)
			return false;
		VkCommandBufferAllocateInfo ai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
		ai.commandPool = pool;
		ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		ai.commandBufferCount = 1;
		VkCommandBuffer cb = VK_NULL_HANDLE;
		bool ok = D<PFN_vkAllocateCommandBuffers>("vkAllocateCommandBuffers")(device, &ai, &cb) == VK_SUCCESS;
		if (ok) {
			VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
			bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			D<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer")(cb, &bi);
			record(cb);
			D<PFN_vkEndCommandBuffer>("vkEndCommandBuffer")(cb);
			VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
			si.commandBufferCount = 1;
			si.pCommandBuffers = &cb;
			ok = D<PFN_vkQueueSubmit>("vkQueueSubmit")(queue, 1, &si, VK_NULL_HANDLE) == VK_SUCCESS;
			Flush();
		}
		D<PFN_vkDestroyCommandPool>("vkDestroyCommandPool")(device, pool, nullptr);
		return ok;
	}

	void Barrier(VkCommandBuffer cb, VkImage image, VkImageAspectFlags aspect, VkImageLayout from, VkImageLayout to)
	{
		VkImageMemoryBarrier b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
		b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
		b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
		b.oldLayout = from;
		b.newLayout = to;
		b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		b.image = image;
		b.subresourceRange = { aspect, 0, 1, 0, VK_REMAINING_ARRAY_LAYERS };
		D<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		    0, 0, nullptr, 0, nullptr, 1, &b);
	}

	// An image with its own memory, kept until the GfxApi goes
	VkImage MakeImage(VkFormat format, uint32_t w, uint32_t h, uint32_t layers, VkImageUsageFlags usage)
	{
		VkImageCreateInfo ici{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		ici.imageType = VK_IMAGE_TYPE_2D;
		ici.format = format;
		ici.extent = { w, h, 1 };
		ici.mipLevels = 1;
		ici.arrayLayers = layers;
		ici.samples = VK_SAMPLE_COUNT_1_BIT;
		ici.tiling = VK_IMAGE_TILING_OPTIMAL;
		ici.usage = usage;
		ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkImage image = VK_NULL_HANDLE;
		if (D<PFN_vkCreateImage>("vkCreateImage")(device, &ici, nullptr, &image) != VK_SUCCESS)
			return VK_NULL_HANDLE;
		VkMemoryRequirements req;
		D<PFN_vkGetImageMemoryRequirements>("vkGetImageMemoryRequirements")(device, image, &req);
		VkMemoryAllocateInfo mai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
		mai.allocationSize = req.size;
		int32_t type = MemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		mai.memoryTypeIndex = (uint32_t)type;
		VkDeviceMemory mem = VK_NULL_HANDLE;
		if (type < 0 || D<PFN_vkAllocateMemory>("vkAllocateMemory")(device, &mai, nullptr, &mem) != VK_SUCCESS
		    || D<PFN_vkBindImageMemory>("vkBindImageMemory")(device, image, mem, 0) != VK_SUCCESS) {
			D<PFN_vkDestroyImage>("vkDestroyImage")(device, image, nullptr);
			if (mem)
				D<PFN_vkFreeMemory>("vkFreeMemory")(device, mem, nullptr);
			return VK_NULL_HANDLE;
		}
		made.push_back({ image, mem });
		return image;
	}

	GfxTexture Wrap(VkImage image, VkFormat format, uint32_t w, uint32_t h, uint32_t samples)
	{
		GfxTexture g;
		g.tex.eType = vr::TextureType_Vulkan;
		g.tex.eColorSpace = vr::ColorSpace_Auto;
		g.vk.m_nImage = (uint64_t)image;
		g.vk.m_pDevice = (VkDevice_T*)device;
		g.vk.m_pPhysicalDevice = (VkPhysicalDevice_T*)physical;
		g.vk.m_pInstance = (VkInstance_T*)instance;
		g.vk.m_pQueue = (VkQueue_T*)queue;
		g.vk.m_nQueueFamilyIndex = family;
		g.vk.m_nWidth = w;
		g.vk.m_nHeight = h;
		g.vk.m_nFormat = (uint32_t)format;
		g.vk.m_nSampleCount = samples;
		g.keep = image ? (void*)(uintptr_t)(uint64_t)image : nullptr;
		g.Texture();
		return g;
	}

	GfxTexture Pattern(uint32_t w, uint32_t h, int64_t nativeFormat, int eye, uint32_t samples, uint32_t arraySize) override
	{
		gfx::Texels texels = TexelsOfVk(nativeFormat);
		auto format = (VkFormat)nativeFormat;
		if (texels == gfx::Texels::Unknown || !w || !h || samples > 1)
			return Wrap(VK_NULL_HANDLE, format, w, h, samples); // multisampled patterns: D3D11 only for now
		arraySize = arraySize ? arraySize : 1;
		VkImage image = MakeImage(format, w, h, arraySize,
		    VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		if (!image)
			return Wrap(VK_NULL_HANDLE, format, w, h, 1);
		size_t pitch = (size_t)w * gfx::BytesOf(texels);
		std::vector<uint8_t> data = gfx::PatternTexels(texels, w, h, eye, pitch, false);

		VkBufferCreateInfo bci{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		bci.size = data.size();
		bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		VkBuffer buffer = VK_NULL_HANDLE;
		VkDeviceMemory mem = VK_NULL_HANDLE;
		bool ok = D<PFN_vkCreateBuffer>("vkCreateBuffer")(device, &bci, nullptr, &buffer) == VK_SUCCESS;
		if (ok) {
			VkMemoryRequirements req;
			D<PFN_vkGetBufferMemoryRequirements>("vkGetBufferMemoryRequirements")(device, buffer, &req);
			int32_t type = MemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			VkMemoryAllocateInfo mai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
			mai.allocationSize = req.size;
			mai.memoryTypeIndex = (uint32_t)type;
			void* mapped = nullptr;
			ok = type >= 0 && D<PFN_vkAllocateMemory>("vkAllocateMemory")(device, &mai, nullptr, &mem) == VK_SUCCESS
			    && D<PFN_vkBindBufferMemory>("vkBindBufferMemory")(device, buffer, mem, 0) == VK_SUCCESS
			    && D<PFN_vkMapMemory>("vkMapMemory")(device, mem, 0, bci.size, 0, &mapped) == VK_SUCCESS;
			if (ok) {
				std::memcpy(mapped, data.data(), data.size());
				D<PFN_vkUnmapMemory>("vkUnmapMemory")(device, mem);
			}
		}
		if (ok) {
			ok = Run([&](VkCommandBuffer cb) {
				Barrier(cb, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
				std::vector<VkBufferImageCopy> regions;
				for (uint32_t layer = 0; layer < arraySize; layer++) {
					VkBufferImageCopy r{};
					r.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, layer, 1 };
					r.imageExtent = { w, h, 1 };
					regions.push_back(r);
				}
				D<PFN_vkCmdCopyBufferToImage>("vkCmdCopyBufferToImage")(cb, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				    (uint32_t)regions.size(), regions.data());
				Barrier(cb, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
			});
		}
		if (buffer)
			D<PFN_vkDestroyBuffer>("vkDestroyBuffer")(device, buffer, nullptr);
		if (mem)
			D<PFN_vkFreeMemory>("vkFreeMemory")(device, mem, nullptr);
		return Wrap(ok ? image : VK_NULL_HANDLE, format, w, h, 1);
	}

	GfxTexture Depth(uint32_t w, uint32_t h, float value) override
	{
		VkImage image = MakeImage(VK_FORMAT_D32_SFLOAT, w, h, 1,
		    VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		bool ok = image && Run([&](VkCommandBuffer cb) {
			Barrier(cb, image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
			VkClearDepthStencilValue clear{ value, 0 };
			VkImageSubresourceRange range{ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
			D<PFN_vkCmdClearDepthStencilImage>("vkCmdClearDepthStencilImage")(cb, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);
			Barrier(cb, image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
		});
		return Wrap(ok ? image : VK_NULL_HANDLE, VK_FORMAT_D32_SFLOAT, w, h, 1);
	}

	void Flush() override
	{
		if (queue)
			D<PFN_vkQueueWaitIdle>("vkQueueWaitIdle")(queue);
	}
};

} // namespace

std::unique_ptr<GfxApi> MakeVkGfx(std::string* whyNot)
{
	auto g = std::make_unique<VkGfx>();
	if (!g->Init(whyNot))
		return nullptr;
	return g;
}
