// D3D12 for the frame cases: a device on the first adapter (WARP without one) and a direct queue; textures filled
// from an upload buffer and left in the pixel shader resource state, as OpenVR's D3D12 games submit them.

#include "GfxApi.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <d3d12.h>
#include <dxgi1_4.h>

namespace {

class D3D12Gfx : public GfxApi {
public:
	ID3D12Device* device = nullptr;
	ID3D12CommandQueue* queue = nullptr;
	ID3D12Fence* fence = nullptr;
	UINT64 fenceValue = 0;
	bool stuck = false; // a copy didn't finish in time: nothing it uses is released
	std::vector<ID3D12Resource*> made;

	~D3D12Gfx() override
	{
		Flush();
		if (stuck)
			return; // the GPU may still use them: left alive
		for (ID3D12Resource* r : made)
			r->Release();
		if (fence)
			fence->Release();
		if (queue)
			queue->Release();
		if (device)
			device->Release();
	}

	GfxTexture Wrap(ID3D12Resource* r)
	{
		GfxTexture g;
		g.tex.eType = vr::TextureType_DirectX12;
		g.tex.eColorSpace = vr::ColorSpace_Auto;
		g.d3d12.m_pResource = r;
		g.d3d12.m_pCommandQueue = queue;
		g.d3d12.m_nNodeMask = 0;
		g.keep = r;
		if (r)
			made.push_back(r);
		g.Texture();
		return g;
	}

	// Records commands into a list, runs them on the queue and waits for them
	template <typename F>
	bool Run(F record)
	{
		ID3D12CommandAllocator* alloc = nullptr;
		ID3D12GraphicsCommandList* list = nullptr;
		bool ok = SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc)))
		    && SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, nullptr, IID_PPV_ARGS(&list)));
		if (ok) {
			record(list);
			ok = SUCCEEDED(list->Close());
		}
		if (ok) {
			ID3D12CommandList* lists[] = { list };
			queue->ExecuteCommandLists(1, lists);
			Flush();
			if (fence->GetCompletedValue() < fenceValue) {
				// Still in flight after Flush's 5 s: the list and allocator (and the caller's upload buffer) stay alive
				stuck = true;
				return false;
			}
		}
		if (list)
			list->Release();
		if (alloc)
			alloc->Release();
		return ok;
	}

	ID3D12Resource* Upload(size_t size)
	{
		D3D12_HEAP_PROPERTIES heap{ D3D12_HEAP_TYPE_UPLOAD };
		D3D12_RESOURCE_DESC bd{};
		bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		bd.Width = size;
		bd.Height = 1;
		bd.DepthOrArraySize = 1;
		bd.MipLevels = 1;
		bd.SampleDesc.Count = 1;
		bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		ID3D12Resource* buffer = nullptr;
		if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&buffer))))
			return nullptr;
		return buffer;
	}

	GfxTexture Pattern(uint32_t w, uint32_t h, int64_t nativeFormat, int eye, uint32_t samples, uint32_t arraySize) override
	{
		gfx::Texels texels = TexelsOfDxgi(nativeFormat);
		if (texels == gfx::Texels::Unknown || !w || !h || samples > 1)
			return Wrap(nullptr); // multisampled patterns: D3D11 only for now
		arraySize = arraySize ? arraySize : 1;
		D3D12_RESOURCE_DESC desc{};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.Width = w;
		desc.Height = h;
		desc.DepthOrArraySize = (UINT16)arraySize;
		desc.MipLevels = 1;
		desc.Format = (DXGI_FORMAT)nativeFormat;
		desc.SampleDesc.Count = 1;
		desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		D3D12_HEAP_PROPERTIES heap{ D3D12_HEAP_TYPE_DEFAULT };
		ID3D12Resource* tex = nullptr;
		if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex))))
			return Wrap(nullptr);
		size_t pitch = ((size_t)w * gfx::BytesOf(texels) + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) & ~(size_t)(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
		std::vector<uint8_t> data = gfx::PatternTexels(texels, w, h, eye, pitch, false);
		ID3D12Resource* upload = Upload(data.size());
		void* mapped = nullptr;
		if (!upload || FAILED(upload->Map(0, nullptr, &mapped))) {
			if (upload)
				upload->Release();
			tex->Release();
			return Wrap(nullptr);
		}
		std::memcpy(mapped, data.data(), data.size());
		upload->Unmap(0, nullptr);
		bool ok = Run([&](ID3D12GraphicsCommandList* list) {
			for (uint32_t layer = 0; layer < arraySize; layer++) {
				D3D12_TEXTURE_COPY_LOCATION src{};
				src.pResource = upload;
				src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
				src.PlacedFootprint.Footprint = { desc.Format, w, h, 1, (UINT)pitch };
				D3D12_TEXTURE_COPY_LOCATION dst{};
				dst.pResource = tex;
				dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
				dst.SubresourceIndex = layer;
				list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
			}
			D3D12_RESOURCE_BARRIER b{};
			b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			b.Transition.pResource = tex;
			b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
			b.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			list->ResourceBarrier(1, &b);
		});
		if (stuck)
			return Wrap(nullptr); // upload and tex left alive: the GPU may still use them
		upload->Release();
		if (!ok) {
			tex->Release();
			return Wrap(nullptr);
		}
		return Wrap(tex);
	}

	GfxTexture Depth(uint32_t w, uint32_t h, float value) override
	{
		D3D12_RESOURCE_DESC desc{};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.Width = w;
		desc.Height = h;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.Format = DXGI_FORMAT_D32_FLOAT;
		desc.SampleDesc.Count = 1;
		desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		D3D12_HEAP_PROPERTIES heap{ D3D12_HEAP_TYPE_DEFAULT };
		D3D12_CLEAR_VALUE clear{};
		clear.Format = DXGI_FORMAT_D32_FLOAT;
		clear.DepthStencil.Depth = value;
		ID3D12Resource* tex = nullptr;
		if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&tex))))
			return Wrap(nullptr);
		ID3D12DescriptorHeap* dsvHeap = nullptr;
		D3D12_DESCRIPTOR_HEAP_DESC hd{};
		hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		hd.NumDescriptors = 1;
		if (SUCCEEDED(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&dsvHeap)))) {
			D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsvHeap->GetCPUDescriptorHandleForHeapStart();
			device->CreateDepthStencilView(tex, nullptr, dsv);
			Run([&](ID3D12GraphicsCommandList* list) {
				list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, value, 0, 0, nullptr);
				D3D12_RESOURCE_BARRIER b{};
				b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				b.Transition.pResource = tex;
				b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				b.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
				b.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
				list->ResourceBarrier(1, &b);
			});
			dsvHeap->Release();
		}
		return Wrap(tex);
	}

	void Flush() override
	{
		if (!queue || !fence)
			return;
		fenceValue++;
		HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (event && SUCCEEDED(queue->Signal(fence, fenceValue)) && SUCCEEDED(fence->SetEventOnCompletion(fenceValue, event)))
			WaitForSingleObject(event, 5000); // at most 5 s
		if (event)
			CloseHandle(event);
	}
};

} // namespace

std::unique_ptr<GfxApi> MakeD3D12Gfx(std::string* whyNot)
{
	auto g = std::make_unique<D3D12Gfx>();
	IDXGIFactory4* factory = nullptr;
	if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory4), (void**)&factory))) {
		*whyNot = "no DXGI 1.4 factory";
		return nullptr;
	}
	IDXGIAdapter1* adapter = nullptr;
	if (factory->EnumAdapters1(0, &adapter) == S_OK) {
		if (FAILED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g->device))))
			g->device = nullptr;
		adapter->Release();
	}
	if (!g->device) {
		IDXGIAdapter* warp = nullptr;
		if (SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)))) {
			if (FAILED(D3D12CreateDevice(warp, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g->device))))
				g->device = nullptr;
			warp->Release();
		}
	}
	factory->Release();
	if (!g->device) {
		*whyNot = "no D3D12 device, not even WARP";
		return nullptr;
	}
	D3D12_COMMAND_QUEUE_DESC qd{};
	qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	if (FAILED(g->device->CreateCommandQueue(&qd, IID_PPV_ARGS(&g->queue))) || FAILED(g->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g->fence)))) {
		*whyNot = "no D3D12 queue";
		return nullptr;
	}
	return g;
}
