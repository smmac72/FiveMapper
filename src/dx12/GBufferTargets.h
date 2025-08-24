#pragma once
#include <wrl/client.h>
#include <d3d12.h>
#include <cstdint>

namespace dx12
{

class GBufferTargets
{
public:
    // g-buffer formats
    struct Formats {
        DXGI_FORMAT g0 = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; // albedo
        DXGI_FORMAT g1 = DXGI_FORMAT_R16G16_FLOAT; // normal.xy + roughness
        DXGI_FORMAT g2 = DXGI_FORMAT_R8G8B8A8_UNORM; // spec/metal/emissive
        DXGI_FORMAT g3 = DXGI_FORMAT_R8G8B8A8_UNORM; // extra reserve
        DXGI_FORMAT dsv = DXGI_FORMAT_D32_FLOAT; // depth
    };

    void Initialize(ID3D12Device* device, uint32_t width, uint32_t height, const Formats& fmts = {});
    void Shutdown();

    D3D12_CPU_DESCRIPTOR_HANDLE GetRTV(size_t i) const { return { m_rtvHeap->GetCPUDescriptorHandleForHeapStart().ptr + i * m_rtvStride }; }
    D3D12_CPU_DESCRIPTOR_HANDLE GetDSV() const { return m_dsvHeap->GetCPUDescriptorHandleForHeapStart(); }

    ID3D12DescriptorHeap*       GetSrvHeap() const { return m_srvHeap.Get(); }
    D3D12_GPU_DESCRIPTOR_HANDLE GetSrvTableGPUStart() const { return m_srvHeap->GetGPUDescriptorHandleForHeapStart(); }

    ID3D12Resource* GetTex(size_t i) const { return m_gbuf[i].Get(); }

    uint32_t Width() const { return m_width; }
    uint32_t Height() const { return m_height; }

private:
    void createHeaps(ID3D12Device* device);
    void createResources(ID3D12Device* device, const Formats& fmts);

    Microsoft::WRL::ComPtr<ID3D12Resource>      m_gbuf[4];
    Microsoft::WRL::ComPtr<ID3D12Resource>      m_depth;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;  // 4 RTV
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_dsvHeap;  // 1 DSV
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvHeap;  // 4 SRV (shader-visible)

    uint32_t m_width = 0, m_height = 0;
    UINT     m_rtvStride = 0, m_srvStride = 0;

    Formats  m_formats{};
};

}