#include "callers_support.h"

namespace callers {

Texture::Texture(ArgSet set, CallContext& ctx)
{
	if (set == ArgSet::Null) {
		p = nullptr;
		return;
	}
	tex.eType = vr::TextureType_DirectX;
	tex.eColorSpace = vr::ColorSpace_Auto;
	if (set == ArgSet::Invalid) {
		tex.handle = (void*)1;
		p = &tex;
		return;
	}
#ifdef _WIN32
	if (ctx.d3d11) {
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = desc.Height = 1024;
		desc.MipLevels = desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		ID3D11Texture2D* t = nullptr;
		if (SUCCEEDED(ctx.d3d11->CreateTexture2D(&desc, nullptr, &t))) {
			owner = std::shared_ptr<void>(t, [](void* x) { ((ID3D11Texture2D*)x)->Release(); });
			tex.handle = t;
		}
	}
#endif
	p = &tex;
}

} // namespace callers
