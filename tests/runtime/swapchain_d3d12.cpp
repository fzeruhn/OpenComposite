// The stand-in runtime's D3D12 sessions (XR_KHR_D3D12_enable): swapchain images made on the app's device, read back
// through a copy on the app's queue.

#include "runtime_internal.h"

#include "probe.h"

#include <dxgi1_4.h>

#include <algorithm>
#include <cstring>

namespace {

XrResult XRAPI_CALL GetD3D12GraphicsRequirements(XrInstance, XrSystemId, XrGraphicsRequirementsD3D12KHR* reqs)
{
	XrResult injected;
	if (ScriptFails("xrGetD3D12GraphicsRequirementsKHR", &injected))
		return injected;
	{
		std::lock_guard<std::recursive_mutex> g(St().lock);
		St().requirementsQueried.insert(RtApi::D3D12);
	}
	IDXGIFactory1* factory = nullptr;
	if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory)))
		return XR_ERROR_RUNTIME_FAILURE;
	IDXGIAdapter1* adapter = nullptr;
	XrResult result = XR_ERROR_RUNTIME_FAILURE;
	if (factory->EnumAdapters1(0, &adapter) == S_OK) {
		DXGI_ADAPTER_DESC1 desc;
		adapter->GetDesc1(&desc);
		// The first adapter, as Monado names it
		reqs->adapterLuid = desc.AdapterLuid;
		reqs->minFeatureLevel = D3D_FEATURE_LEVEL_11_0;
		adapter->Release();
		std::lock_guard<std::recursive_mutex> g(St().lock);
		St().requiredLuid = desc.AdapterLuid;
		result = XR_SUCCESS;
	}
	factory->Release();
	return result;
}

XrResult Bind(const XrBaseInStructure* binding, RtSession* s)
{
	auto* b = (const XrGraphicsBindingD3D12KHR*)binding;
	if (!b->device || !b->queue)
		return XR_ERROR_GRAPHICS_DEVICE_INVALID;
	// The device must be on the adapter the requirements named (Monado: XR_ERROR_GRAPHICS_DEVICE_INVALID)
	LUID luid = b->device->GetAdapterLuid();
	if (std::memcmp(&luid, &St().requiredLuid, sizeof(LUID)) != 0)
		return XR_ERROR_GRAPHICS_DEVICE_INVALID;
	s->d3d12Device = b->device;
	s->d3d12Queue = b->queue;
	s->d3d12Device->AddRef();
	s->d3d12Queue->AddRef();
	return XR_SUCCESS;
}

void Unbind(RtSession* s)
{
	if (s->d3d12Queue)
		s->d3d12Queue->Release();
	if (s->d3d12Device)
		s->d3d12Device->Release();
	s->d3d12Queue = nullptr;
	s->d3d12Device = nullptr;
}

const std::vector<int64_t>& Formats()
{
	static const std::vector<int64_t> formats = {
		DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
		DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
		DXGI_FORMAT_R8G8B8A8_UNORM,
		DXGI_FORMAT_B8G8R8A8_UNORM,
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		DXGI_FORMAT_R10G10B10A2_UNORM,
		DXGI_FORMAT_D32_FLOAT,
	};
	return formats;
}

const std::vector<int64_t>& DefaultFormats()
{
	static const std::vector<int64_t> formats = { DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM,
		DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB };
	return formats;
}

bool IsDepth(int64_t format)
{
	return LayoutOfDxgi(format) == PixelLayout::Depth;
}

void Destroy(RtSession*, RtSwapchain* sc)
{
	for (uint64_t image : sc->images)
		((ID3D12Resource*)(uintptr_t)image)->Release();
	sc->images.clear();
}

XrResult Create(RtSession* s, const XrSwapchainCreateInfo* info, RtSwapchain* sc)
{
	if (!s->d3d12Device)
		return XR_ERROR_VALIDATION_FAILURE;
	if (std::find(Formats().begin(), Formats().end(), info->format) == Formats().end())
		return XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED;
	bool depth = IsDepth(info->format);
	D3D12_RESOURCE_DESC desc{};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = info->width;
	desc.Height = info->height;
	desc.DepthOrArraySize = (UINT16)((info->arraySize ? info->arraySize : 1) * (info->faceCount ? info->faceCount : 1));
	desc.MipLevels = (UINT16)(info->mipCount ? info->mipCount : 1);
	desc.Format = (DXGI_FORMAT)info->format;
	desc.SampleDesc.Count = info->sampleCount ? info->sampleCount : 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	desc.Flags = depth ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	if (!depth && desc.SampleDesc.Count == 1)
		desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	D3D12_HEAP_PROPERTIES heap{};
	heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	// The spec's state for a released image: render target (depth write for depth)
	D3D12_RESOURCE_STATES state = depth ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_RENDER_TARGET;
	sc->info = *info;
	for (int i = 0; i < 3; i++) {
		ID3D12Resource* res = nullptr;
		HRESULT hr = s->d3d12Device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&res));
		if (FAILED(hr) && (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)) {
			// Not every format allows unordered access: without it
			desc.Flags &= ~D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
			hr = s->d3d12Device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&res));
		}
		if (FAILED(hr)) {
			Destroy(s, sc);
			return XR_ERROR_RUNTIME_FAILURE;
		}
		sc->images.push_back((uint64_t)(uintptr_t)res);
	}
	return XR_SUCCESS;
}

void Enumerate(const RtSwapchain* sc, XrSwapchainImageBaseHeader* images)
{
	auto* out = (XrSwapchainImageD3D12KHR*)images;
	for (size_t i = 0; i < sc->images.size(); i++)
		out[i].texture = (ID3D12Resource*)(uintptr_t)sc->images[i];
}

template <typename T>
void Release(T*& p)
{
	if (p)
		p->Release();
	p = nullptr;
}

bool Read(RtSession* s, RtSwapchain* sc, uint32_t index, uint32_t arrayIndex, const std::vector<XrOffset2Di>& points,
    uint32_t bytes, std::vector<uint8_t>& out, std::string* why)
{
	*why = "the D3D12 readback failed";
	if (!s->d3d12Device || !s->d3d12Queue || index >= sc->images.size() || points.empty())
		return false;
	auto* res = (ID3D12Resource*)(uintptr_t)sc->images[index];
	D3D12_RESOURCE_DESC desc = res->GetDesc();
	if (desc.SampleDesc.Count > 1 || arrayIndex >= desc.DepthOrArraySize)
		return false; // a multisampled image isn't read back
	ID3D12Device* dev = s->d3d12Device;
	D3D12_COMMAND_LIST_TYPE type = s->d3d12Queue->GetDesc().Type;

	// A readback buffer with one placed footprint per point
	const UINT64 slot = D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;
	D3D12_HEAP_PROPERTIES heap{};
	heap.Type = D3D12_HEAP_TYPE_READBACK;
	D3D12_RESOURCE_DESC bd{};
	bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bd.Width = slot * points.size();
	bd.Height = 1;
	bd.DepthOrArraySize = 1;
	bd.MipLevels = 1;
	bd.SampleDesc.Count = 1;
	bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	ID3D12Resource* buffer = nullptr;
	ID3D12CommandAllocator* alloc = nullptr;
	ID3D12GraphicsCommandList* list = nullptr;
	ID3D12Fence* fence = nullptr;
	HANDLE event = nullptr;
	bool ok = SUCCEEDED(dev->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&buffer)))
	    && SUCCEEDED(dev->CreateCommandAllocator(type, IID_PPV_ARGS(&alloc)))
	    && SUCCEEDED(dev->CreateCommandList(0, type, alloc, nullptr, IID_PPV_ARGS(&list)))
	    && SUCCEEDED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
	if (ok) {
		UINT sub = arrayIndex * desc.MipLevels;
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = res;
		barrier.Transition.Subresource = sub;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
		list->ResourceBarrier(1, &barrier);
		for (size_t i = 0; i < points.size(); i++) {
			UINT x = (UINT)std::min<int64_t>(std::max<int32_t>(points[i].x, 0), (int64_t)desc.Width - 1);
			UINT y = (UINT)std::min<int64_t>(std::max<int32_t>(points[i].y, 0), (int64_t)desc.Height - 1);
			D3D12_TEXTURE_COPY_LOCATION dst{};
			dst.pResource = buffer;
			dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			dst.PlacedFootprint.Offset = slot * i;
			dst.PlacedFootprint.Footprint = { desc.Format, 1, 1, 1, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT };
			D3D12_TEXTURE_COPY_LOCATION src{};
			src.pResource = res;
			src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			src.SubresourceIndex = sub;
			D3D12_BOX box{ x, y, 0, x + 1, y + 1, 1 };
			list->CopyTextureRegion(&dst, 0, 0, 0, &src, &box);
		}
		std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
		list->ResourceBarrier(1, &barrier);
		ok = SUCCEEDED(list->Close());
	}
	if (ok) {
		ID3D12CommandList* lists[] = { list };
		s->d3d12Queue->ExecuteCommandLists(1, lists);
		event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		ok = event && SUCCEEDED(s->d3d12Queue->Signal(fence, 1)) && SUCCEEDED(fence->SetEventOnCompletion(1, event))
		    && WaitForSingleObject(event, 5000) == WAIT_OBJECT_0; // at most 5 s
		if (!ok && fence->GetCompletedValue() < 1) {
			// Still in flight: the list, allocator, buffer and fence stay alive (left, not freed under the GPU)
			RtNoteReadbackTimeout();
			*why = "the D3D12 readback timed out";
			if (event)
				CloseHandle(event);
			return false;
		}
	}
	if (ok) {
		void* data = nullptr;
		D3D12_RANGE range{ 0, (SIZE_T)bd.Width };
		ok = SUCCEEDED(buffer->Map(0, &range, &data));
		if (ok) {
			out.clear();
			for (size_t i = 0; i < points.size(); i++)
				out.insert(out.end(), (const uint8_t*)data + slot * i, (const uint8_t*)data + slot * i + bytes);
			D3D12_RANGE none{ 0, 0 };
			buffer->Unmap(0, &none);
		}
	}
	if (event)
		CloseHandle(event);
	Release(fence);
	Release(list);
	Release(alloc);
	Release(buffer);
	return ok;
}

} // namespace

const RtGfx& RtD3D12Gfx()
{
	static const RtGfx gfx = {
		RtApi::D3D12, "d3d12", "XR_KHR_D3D12_enable", nullptr, XR_TYPE_GRAPHICS_BINDING_D3D12_KHR, XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR,
		Bind, Unbind, Formats, DefaultFormats, Create, Destroy, Enumerate, LayoutOfDxgi, false, Read,
	};
	return gfx;
}

PFN_xrVoidFunction RtD3D12Lookup(const char* name)
{
	if (!std::strcmp(name, "xrGetD3D12GraphicsRequirementsKHR"))
		return (PFN_xrVoidFunction)GetD3D12GraphicsRequirements;
	return nullptr;
}
