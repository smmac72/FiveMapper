#pragma once
#include <wrl/client.h>
#include <d3d12.h>
#include <cstdint>

namespace dx12
{

class ShadowMap
{
public:
    ShadowMap() = default;

    void Initialize(ID3D12Device* device, uint32_t width, uint32_t height);
    void Resize(ID3D12Device* device, uint32_t width, uint32_t height);

    ID3D12Resource* GetResource() const { return m_tex.Get(); }

    D3D12_CPU_DESCRIPTOR_HANDLE GetDSV() const { return m_dsvHeap->GetCPUDescriptorHandleForHeapStart(); }
    // cpu-handle for writing SRV into some heap (i'll use GBufferTargets)
    static void CreateSRV(ID3D12Device* device, ID3D12Resource* res, D3D12_CPU_DESCRIPTOR_HANDLE dstCpu);

    uint32_t Width() const { return m_width; }
    uint32_t Height() const { return m_height; }

private:
    void CreateTex(ID3D12Device* device);

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> m_tex;     // R32_TYPELESS
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_dsvHeap;

    uint32_t m_width = 0;
    uint32_t m_height = 0;
};

}
