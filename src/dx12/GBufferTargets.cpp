#include "GBufferTargets.h"
#include <stdexcept>

using Microsoft::WRL::ComPtr;
using namespace dx12;

// helper to create a 2d texture on default heap
static ComPtr<ID3D12Resource> CreateTex2D(
    ID3D12Device* dev,
    DXGI_FORMAT fmt,
    uint32_t width,
    uint32_t height,
    D3D12_RESOURCE_FLAGS flags,
    D3D12_RESOURCE_STATES initialState,
    UINT mipLevels = 1
)
{
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC tex{};
    tex.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    tex.Width = width;
    tex.Height = height;
    tex.DepthOrArraySize = 1;
    tex.MipLevels = static_cast<UINT16>(mipLevels);
    tex.Format = fmt;
    tex.SampleDesc.Count = 1;
    tex.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    tex.Flags = flags;

    ComPtr<ID3D12Resource> res;
    HRESULT hr = dev->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE,
        &tex, initialState,
        nullptr,
        IID_PPV_ARGS(&res)
    );
    if (FAILED(hr))
    {
        throw std::runtime_error("gbuffer: create texture failed");
    }
    return res;
}

void GBufferTargets::Destroy()
{
    for (int i = 0; i < 4; i++)
    {
        m_gbuf[i].Reset();
    }
    m_depth.Reset();
    m_rtvHeap.Reset();
    m_dsvHeap.Reset();
    m_srvHeap.Reset();
    m_rtvStride = m_dsvStride = m_srvStride = 0;
}

void GBufferTargets::Initialize(ID3D12Device* device, uint32_t width, uint32_t height, const Formats& fmts)
{
    m_width  = width;
    m_height = height;

    CreateResources(device, fmts);
    CreateRTVs(device, fmts);
    CreateDSV(device, fmts);
    CreateSRVs(device, fmts);
}

void GBufferTargets::Resize(ID3D12Device* device, uint32_t width, uint32_t height, const Formats& fmts)
{
    Destroy();
    m_width  = width;
    m_height = height;
    Initialize(device, width, height, fmts);
}

void GBufferTargets::CreateResources(ID3D12Device* device, const Formats& fmts)
{
    // create g0..g3 as render target-capable textures
    for (int i = 0; i < 4; i++)
    {
        DXGI_FORMAT fmt = fmts.g0;
        if (i == 1) fmt = fmts.g1;
        if (i == 2) fmt = fmts.g2;
        if (i == 3) fmt = fmts.g3;

        m_gbuf[i] = CreateTex2D(
            device, fmt, m_width, m_height,
            D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
            D3D12_RESOURCE_STATE_COMMON
        );
    }

    // depth as typeless resource; dsv uses D32_FLOAT, srv uses R32_FLOAT
    m_depth = CreateTex2D(
        device, DXGI_FORMAT_R32_TYPELESS, m_width, m_height,
        D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
        D3D12_RESOURCE_STATE_COMMON
    );
}

void GBufferTargets::CreateRTVs(ID3D12Device* device, const Formats& fmts)
{
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = 4;
    rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    if (FAILED(device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&m_rtvHeap))))
    {
        throw std::runtime_error("gbuffer: create rtv heap failed");
    }
    m_rtvStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_CPU_DESCRIPTOR_HANDLE h = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (int i = 0; i < 4; i++)
    {
        D3D12_RENDER_TARGET_VIEW_DESC v{};
        v.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        v.Format = (i == 0 ? fmts.g0 : i == 1 ? fmts.g1 : i == 2 ? fmts.g2 : fmts.g3);
        v.Texture2D.MipSlice = 0;
        v.Texture2D.PlaneSlice = 0;

        device->CreateRenderTargetView(m_gbuf[i].Get(), &v, h);
        h.ptr += m_rtvStride;
    }
}

void GBufferTargets::CreateDSV(ID3D12Device* device, const Formats& fmts)
{
    D3D12_DESCRIPTOR_HEAP_DESC dsvDesc{};
    dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvDesc.NumDescriptors = 1;
    dsvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    if (FAILED(device->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&m_dsvHeap))))
    {
        throw std::runtime_error("gbuffer: create dsv heap failed");
    }
    m_dsvStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

    D3D12_DEPTH_STENCIL_VIEW_DESC d{};
    d.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    d.Format = fmts.dsv; // D32_FLOAT
    d.Flags = D3D12_DSV_FLAG_NONE;
    d.Texture2D.MipSlice = 0;

    device->CreateDepthStencilView(m_depth.Get(), &d, m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
}

void GBufferTargets::CreateSRVs(ID3D12Device* device, const Formats& fmts)
{
    // 5 descriptors: g0..g3, depth
    D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.NumDescriptors = 5;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    if (FAILED(device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&m_srvHeap))))
    {
        throw std::runtime_error("gbuffer: create srv heap failed");
    }
    m_srvStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_CPU_DESCRIPTOR_HANDLE h = m_srvHeap->GetCPUDescriptorHandleForHeapStart();

    // g0..g3 srvs
    for (int i = 0; i < 4; i++)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC s{};
        s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        s.Format = (i == 0 ? fmts.g0 : i == 1 ? fmts.g1 : i == 2 ? fmts.g2 : fmts.g3);
        s.Texture2D.MipLevels = 1;

        device->CreateShaderResourceView(m_gbuf[i].Get(), &s, h);
        h.ptr += m_srvStride;
    }

    // depth srv as R32_FLOAT from R32_TYPELESS resource
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC s{};
        s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        s.Format = DXGI_FORMAT_R32_FLOAT;
        s.Texture2D.MipLevels = 1;

        device->CreateShaderResourceView(m_depth.Get(), &s, h);
    }
}

D3D12_CPU_DESCRIPTOR_HANDLE GBufferTargets::GetRTV(int i) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += i * m_rtvStride;
    return h;
}
