#include "ShadowMap.h"
#include <stdexcept>

using Microsoft::WRL::ComPtr;
using namespace dx12;

void ShadowMap::Initialize(ID3D12Device* device, uint32_t width, uint32_t height)
{
    m_width = width;
    m_height = height;
    CreateTex(device);

    D3D12_DESCRIPTOR_HEAP_DESC dsvDesc{};
    dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvDesc.NumDescriptors = 1;
    if (FAILED(device->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&m_dsvHeap))))
    {
        throw std::runtime_error("shadow: create dsv heap failed");
    }

    D3D12_DEPTH_STENCIL_VIEW_DESC d{};
    d.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    d.Format = DXGI_FORMAT_D32_FLOAT;
    d.Flags = D3D12_DSV_FLAG_NONE;
    device->CreateDepthStencilView(m_tex.Get(), &d, m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
}

void ShadowMap::Resize(ID3D12Device* device, uint32_t width, uint32_t height)
{
    m_tex.Reset();
    m_dsvHeap.Reset();
    Initialize(device, width, height);
}

void ShadowMap::CreateTex(ID3D12Device* device)
{
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC tex{};
    tex.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    tex.Width = m_width;
    tex.Height = m_height;
    tex.DepthOrArraySize = 1;
    tex.MipLevels = 1;
    tex.Format = DXGI_FORMAT_R32_TYPELESS;
    tex.SampleDesc.Count = 1;
    tex.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clear{};
    clear.Format = DXGI_FORMAT_D32_FLOAT;
    clear.DepthStencil.Depth = 1.0f;

    if (FAILED(device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE,
        &tex, D3D12_RESOURCE_STATE_COMMON,
        &clear, IID_PPV_ARGS(&m_tex))))
    {
        throw std::runtime_error("shadow: create texture failed");
    }
}

void ShadowMap::CreateSRV(ID3D12Device* device, ID3D12Resource* res, D3D12_CPU_DESCRIPTOR_HANDLE dstCpu)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC s{};
    s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    s.Format = DXGI_FORMAT_R32_FLOAT;
    s.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(res, &s, dstCpu);
}
