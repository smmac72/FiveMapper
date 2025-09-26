#pragma once
#include <wrl/client.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstdint>

namespace dx12
{

// manages offscreen g-buffer: G0..G3 (rtv+srv) + depth (dsv+srv)
class GBufferTargets
{
public:
    struct Formats
    {
        DXGI_FORMAT g0 = DXGI_FORMAT_R8G8B8A8_UNORM;        // albedo
        DXGI_FORMAT g1 = DXGI_FORMAT_R16G16B16A16_FLOAT;    // normal.xyz + roughness
        DXGI_FORMAT g2 = DXGI_FORMAT_R8G8B8A8_UNORM;        // metallic/ao/emissive
        DXGI_FORMAT g3 = DXGI_FORMAT_R8G8B8A8_UNORM;        // spare/extra
        DXGI_FORMAT dsv = DXGI_FORMAT_D32_FLOAT;            // dsv view format
        // note: depth resource is created as R32_TYPELESS, srv as R32_FLOAT
    };

public:
    GBufferTargets() = default;

    void Initialize(ID3D12Device* device, uint32_t width, uint32_t height, const Formats& fmts);
    void Resize(ID3D12Device* device, uint32_t width, uint32_t height, const Formats& fmts);

    // textures
    ID3D12Resource* GetTex(int i) const { return m_gbuf[i].Get(); }
    ID3D12Resource* GetDepth()   const { return m_depth.Get();    }

    // rtv/dsv handles
    D3D12_CPU_DESCRIPTOR_HANDLE GetRTV(int i) const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetDSV() const { return m_dsvHeap->GetCPUDescriptorHandleForHeapStart(); }

    // srv heap for t0..t4 (g0..g3, depth)
    ID3D12DescriptorHeap* GetSrvHeap() const { return m_srvHeap.Get(); }
    D3D12_GPU_DESCRIPTOR_HANDLE GetSrvTableGPUStart() const { return m_srvHeap->GetGPUDescriptorHandleForHeapStart(); }

    uint32_t Width()  const { return m_width;  }
    uint32_t Height() const { return m_height; }

private:
    void Destroy();

    void CreateResources(ID3D12Device* device, const Formats& fmts);
    void CreateRTVs(ID3D12Device* device, const Formats& fmts);
    void CreateDSV(ID3D12Device* device, const Formats& fmts);
    void CreateSRVs(ID3D12Device* device, const Formats& fmts);

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> m_gbuf[4];
    Microsoft::WRL::ComPtr<ID3D12Resource> m_depth;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;  // 4
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_dsvHeap;  // 1
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvHeap;  // 5 (g0..g3, depth)

    UINT m_rtvStride = 0;
    UINT m_dsvStride = 0;
    UINT m_srvStride = 0;

    uint32_t m_width = 0;
    uint32_t m_height = 0;
};

} // namespace dx12
