#pragma once
#include <wrl/client.h>
#include <d3d12.h>
#include <string>

namespace assets
{

// safe texture loader that never throws. on failure returns nullptr and logs.
struct TextureLoader
{
    // load texture from file. supported: dds, png, jpg, tiff, bmp, etc. via directxtex.
    // if is_srgb == true, the resource will be created in *_unorm_srgb format.
    // cmd_list must be in recording state. after call, resource is in pixel_shader_resource state.
    static Microsoft::WRL::ComPtr<ID3D12Resource>
    LoadFromFile(ID3D12Device* device,
                 ID3D12GraphicsCommandList* cmd_list,
                 const std::wstring& path,
                 bool is_srgb) noexcept;

    // create 1x1 texture filled with rgba (linear inputs). if is_srgb=true, resource format is srgb.
    static Microsoft::WRL::ComPtr<ID3D12Resource>
    CreateSolid1x1(ID3D12Device* device,
                   ID3D12GraphicsCommandList* cmd_list,
                   float r, float g, float b, float a,
                   bool is_srgb) noexcept;

    // create an srv for a 2d texture into a cpu descriptor slot. srv format == resource format.
    static void CreateSRV(ID3D12Device* device,
                          ID3D12Resource* tex,
                          D3D12_CPU_DESCRIPTOR_HANDLE dst_cpu,
                          bool /*is_srgb_ignored*/) noexcept;
};

} // namespace assets
