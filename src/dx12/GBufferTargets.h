#pragma once
#include <wrl/client.h>
#include <d3d12.h>
#include <cstdint>

namespace dx12
{

// this holds 4 color targets (g0..g3) and one depth target for deferred rendering
class GBufferTargets
{
public:
    struct Formats
    {
        // keep these aligned with gta style pbr packing
        DXGI_FORMAT g0 = DXGI_FORMAT_R8G8B8A8_UNORM;        // albedo (will read as srgb later)
        DXGI_FORMAT g1 = DXGI_FORMAT_R16G16B16A16_FLOAT;    // normals/roughness (packed as needed)
        DXGI_FORMAT g2 = DXGI_FORMAT_R8G8B8A8_UNORM;        // orm/emissive packing
        DXGI_FORMAT g3 = DXGI_FORMAT_R8G8B8A8_UNORM;        // extra / reserved
        DXGI_FORMAT dsv = DXGI_FORMAT_D32_FLOAT;            // depth
    };

public:
    void Initialize(ID3D12Device* device, uint32_t width, uint32_t height, const Formats& fmts = {});
    void Shutdown();

    // cpu handles for OMSetRenderTargets
    D3D12_CPU_DESCRIPTOR_HANDLE GetRTV(size_t i) const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetDSV() const;

    // shader-visible heap for sampling g-buffer in lighting pass
    ID3D12DescriptorHeap*       GetSrvHeap() const { return m_srvHeap.Get(); }
    D3D12_GPU_DESCRIPTOR_HANDLE GetSrvTableGPUStart() const;

    // raw resources (for barriers)
    ID3D12Resource* GetTex(size_t i) const { return m_gbuf[i].Get(); }
    ID3D12Resource* GetDepth()      const { return m_depth.Get(); }

    uint32_t Width()  const { return m_width; }
    uint32_t Height() const { return m_height; }
    Formats  GetFormats() const { return m_formats; }

private:
    void createHeaps(ID3D12Device* device);
    void createResources(ID3D12Device* device, const Formats& fmts);

private:
    Microsoft::WRL::ComPtr<ID3D12Resource>       m_gbuf[4];
    Microsoft::WRL::ComPtr<ID3D12Resource>       m_depth;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;   // 4 rtv (cpu)
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_dsvHeap;   // 1 dsv (cpu)
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvHeap;   // 4 srv (shader visible)

    UINT m_rtvStride = 0;
    UINT m_srvStride = 0;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    Formats  m_formats{};
};

}