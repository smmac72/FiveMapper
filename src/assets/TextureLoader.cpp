#include "TextureLoader.h"

#include <DirectXTex.h>      // from directxtex
#include <wrl/client.h>
#include <string>
#include <vector>
#include <cstdio>

// d3dx12 helpers (ensure this header is available in your include paths)
#define NOMINMAX
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#include "d3dx12.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;

static void logw(const std::wstring& s)
{
    OutputDebugStringW(s.c_str());
}

static void loga(const char* s)
{
    OutputDebugStringA(s);
}

namespace
{
    // returns rgba8 unorm or rgba8 unorm srgb depending on flag
    DXGI_FORMAT choose_rgba8(bool srgb) noexcept
    {
        return srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
                    : DXGI_FORMAT_R8G8B8A8_UNORM;
    }

    // convert scratchimage to rgba8 (srgb/unorm) and generate mips
    bool convert_to_rgba8_and_mips(const ScratchImage& src, bool srgb, ScratchImage& out)
    {
        TexMetadata md = src.GetMetadata();
        // for wic sources use force rgb; for dds we keep original then convert
        DXGI_FORMAT dst_fmt = choose_rgba8(srgb);

        ScratchImage converted;
        HRESULT hr = Convert(src.GetImages(), src.GetImageCount(), src.GetMetadata(),
                             dst_fmt, TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, converted);
        if (FAILED(hr))
            return false;

        ScratchImage mipchain;
        hr = GenerateMipMaps(converted.GetImages(), converted.GetImageCount(), converted.GetMetadata(),
                             TEX_FILTER_DEFAULT, 0, mipchain);
        if (FAILED(hr))
            return false;

        out = std::move(mipchain);
        return true;
    }

    // create a default-heap texture and upload scratchimage contents into it
    ComPtr<ID3D12Resource> create_and_upload_2d(ID3D12Device* device,
                                                ID3D12GraphicsCommandList* cmd_list,
                                                const ScratchImage& img)
    {
        const TexMetadata& md = img.GetMetadata();

        // describe texture
        D3D12_RESOURCE_DESC tex_desc = {};
        tex_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        tex_desc.Alignment = 0;
        tex_desc.Width     = static_cast<UINT>(md.width);
        tex_desc.Height    = static_cast<UINT>(md.height);
        tex_desc.DepthOrArraySize = static_cast<UINT16>(md.arraySize);
        tex_desc.MipLevels = static_cast<UINT16>(md.mipLevels);
        tex_desc.Format    = md.format; // already rgba8 or rgba8_srgb
        tex_desc.SampleDesc.Count = 1;
        tex_desc.SampleDesc.Quality = 0;
        tex_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        tex_desc.Flags  = D3D12_RESOURCE_FLAG_NONE;

        // create default heap resource in copy dest state
        D3D12_HEAP_PROPERTIES heap_default = {};
        heap_default.Type = D3D12_HEAP_TYPE_DEFAULT;

        ComPtr<ID3D12Resource> tex;
        HRESULT hr = device->CreateCommittedResource(
            &heap_default, D3D12_HEAP_FLAG_NONE,
            &tex_desc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&tex));
        if (FAILED(hr))
        {
            loga("[tex] createcommittedresource (default) failed\n");
            return nullptr;
        }

        // create upload buffer
        UINT64 upload_size = GetRequiredIntermediateSize(tex.Get(), 0, static_cast<UINT>(img.GetImageCount()));

        D3D12_HEAP_PROPERTIES heap_upload = {};
        heap_upload.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC upload_desc = {};
        upload_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        upload_desc.Width = upload_size;
        upload_desc.Height = 1;
        upload_desc.DepthOrArraySize = 1;
        upload_desc.MipLevels = 1;
        upload_desc.SampleDesc.Count = 1;
        upload_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ComPtr<ID3D12Resource> upload;
        hr = device->CreateCommittedResource(
            &heap_upload, D3D12_HEAP_FLAG_NONE,
            &upload_desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&upload));
        if (FAILED(hr))
        {
            loga("[tex] createcommittedresource (upload) failed\n");
            return nullptr;
        }

        // fill subresources
        std::vector<D3D12_SUBRESOURCE_DATA> subs;
        subs.reserve(img.GetImageCount());
        for (size_t i = 0; i < img.GetImageCount(); ++i)
        {
            const Image* im = img.GetImages() + i;
            D3D12_SUBRESOURCE_DATA s{};
            s.pData = im->pixels;
            s.RowPitch = im->rowPitch;
            s.SlicePitch = im->slicePitch;
            subs.push_back(s);
        }

        // upload and transition to pixel shader
        UpdateSubresources(cmd_list, tex.Get(), upload.Get(), 0, 0,
                           static_cast<UINT>(subs.size()), subs.data());

        CD3DX12_RESOURCE_BARRIER b = CD3DX12_RESOURCE_BARRIER::Transition(
            tex.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cmd_list->ResourceBarrier(1, &b);

        // upload buffer can be released; gpu will keep ref via cmd list execution
        return tex;
    }
} // anon

namespace assets
{

ComPtr<ID3D12Resource> TextureLoader::LoadFromFile(ID3D12Device* device,
                                                   ID3D12GraphicsCommandList* cmd_list,
                                                   const std::wstring& path,
                                                   bool is_srgb) noexcept
{
    try
    {
        // load source image
        ScratchImage src;
        HRESULT hr = E_FAIL;

        // prefer dds loader if extension is .dds
        bool is_dds = false;
        if (path.size() >= 4)
        {
            std::wstring ext = path.substr(path.size() - 4);
            for (auto& ch : ext) ch = (wchar_t)tolower(ch);
            is_dds = (ext == L".dds");
        }

        if (is_dds)
        {
            hr = LoadFromDDSFile(path.c_str(), DDS_FLAGS_NONE, nullptr, src);
        }
        else
        {
            hr = LoadFromWICFile(path.c_str(), WIC_FLAGS_FORCE_RGB, nullptr, src);
        }

        if (FAILED(hr))
        {
            logw(L"[tex] failed to load image: " + path + L"\n");
            return nullptr;
        }

        // convert to rgba8 (srgb/unorm) + generate mipmaps
        ScratchImage rgba_mips;
        if (!convert_to_rgba8_and_mips(src, is_srgb, rgba_mips))
        {
            logw(L"[tex] convert/mip failed: " + path + L"\n");
            return nullptr;
        }

        // create default texture and upload
        ComPtr<ID3D12Resource> tex = create_and_upload_2d(device, cmd_list, rgba_mips);
        if (!tex)
        {
            logw(L"[tex] create/upload failed: " + path + L"\n");
            return nullptr;
        }

        return tex;
    }
    catch (...)
    {
        logw(L"[tex] exception in loadfromfile: " + path + L"\n");
        return nullptr;
    }
}

ComPtr<ID3D12Resource> TextureLoader::CreateSolid1x1(ID3D12Device* device,
                                                     ID3D12GraphicsCommandList* cmd_list,
                                                     float r, float g, float b, float a,
                                                     bool is_srgb) noexcept
{
    try
    {
        const DXGI_FORMAT fmt = choose_rgba8(is_srgb);

        // fill one pixel (convert linear -> srgb if resource is srgb so that sampling matches)
        auto linear_to_srgb = [](float c) -> uint8_t
        {
            c = (c < 0.0f) ? 0.0f : (c > 1.0f ? 1.0f : c);
            float out = (c <= 0.0031308f) ? (12.92f * c)
                                          : (1.055f * powf(c, 1.0f / 2.4f) - 0.055f);
            int u = static_cast<int>(out * 255.0f + 0.5f);
            if (u < 0) u = 0; if (u > 255) u = 255;
            return static_cast<uint8_t>(u);
        };

        uint8_t px[4];
        if (fmt == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
        {
            px[0] = linear_to_srgb(r);
            px[1] = linear_to_srgb(g);
            px[2] = linear_to_srgb(b);
            px[3] = static_cast<uint8_t>(std::max(0.0f, std::min(1.0f, a)) * 255.0f + 0.5f);
        }
        else
        {
            auto to8 = [](float c) -> uint8_t
            {
                int u = static_cast<int>(std::max(0.0f, std::min(1.0f, c)) * 255.0f + 0.5f);
                if (u < 0) u = 0; if (u > 255) u = 255;
                return static_cast<uint8_t>(u);
            };
            px[0] = to8(r); px[1] = to8(g); px[2] = to8(b); px[3] = to8(a);
        }

        // build scratch image 1x1 with mip chain (just level 1)
        TexMetadata md{};
        md.width = 1; md.height = 1; md.depth = 1;
        md.arraySize = 1; md.mipLevels = 1;
        md.format = fmt; md.dimension = TEX_DIMENSION_TEXTURE2D;

        ScratchImage img;
        HRESULT hr = img.Initialize(md);
        if (FAILED(hr))
            return nullptr;

        memcpy(img.GetImages()->pixels, px, 4);

        // create and upload
        return create_and_upload_2d(device, cmd_list, img);
    }
    catch (...)
    {
        loga("[tex] exception in createsolid1x1\n");
        return nullptr;
    }
}

void TextureLoader::CreateSRV(ID3D12Device* device,
                              ID3D12Resource* tex,
                              D3D12_CPU_DESCRIPTOR_HANDLE dst_cpu,
                              bool /*is_srgb_ignored*/) noexcept
{
    if (!device || !tex) return;

    D3D12_SHADER_RESOURCE_VIEW_DESC s{};
    s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;

    auto desc = tex->GetDesc();
    s.Format = desc.Format; // must match the resource format in d3d12
    s.Texture2D.MipLevels = desc.MipLevels;
    s.Texture2D.MostDetailedMip = 0;
    s.Texture2D.ResourceMinLODClamp = 0.0f;

    device->CreateShaderResourceView(tex, &s, dst_cpu);
}

} // namespace assets
