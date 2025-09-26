#include "GBufferTargets.h"
#include <stdexcept>

using Microsoft::WRL::ComPtr;
using namespace dx12;

static ComPtr<ID3D12Resource> CreateTex2D(
    ID3D12Device* dev,
    DXGI_FORMAT fmt,
    uint32_t width,
    uint32_t height,
    D3D12_RESOURCE_FLAGS flags,
    D3D12_RESOURCE_STATES initialState
)
{
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC tex{};
    tex.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    tex.Width = width;
    tex.Height = height;
    tex.DepthOrArraySize = 1;
    tex.MipLevels = 1;
    tex.Format = fmt;
    tex.SampleDesc.Count = 1;
    tex.Flags = flags;

    ComPtr<ID3D12Resource> res;
    if (FAILED(dev->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &tex, initialState, nullptr, IID_PPV_ARGS(&res))))
    {
        throw std::runtime_error("gbuffer: create texture failed");
    }
    return res;
}

void GBufferTargets::Destroy()
{
    for (int i = 0; i < 4; i++) m_gbuf[i].Reset();
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
    Initialize(device, width, height, fmts);
}

void GBufferTargets::CreateResources(ID3D12Device* device, const Formats& fmts)
{
    for (int i = 0; i < 4; i++)
    {
        DXGI_FORMAT f = (i==0?fmts.g0: i==1?fmts.g1: i==2?fmts.g2: fmts.g3);
        m_gbuf[i] = CreateTex2D(device, f, m_width, m_height,
                                D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                                D3D12_RESOURCE_STATE_COMMON);
    }
    m_depth = CreateTex2D(device, DXGI_FORMAT_R32_TYPELESS, m_width, m_height,
                          D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
                          D3D12_RESOURCE_STATE_COMMON);
}

void GBufferTargets::CreateRTVs(ID3D12Device* device, const Formats& fmts)
{
    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    desc.NumDescriptors = 4;

    if (FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_rtvHeap))))
        throw std::runtime_error("gbuffer: create rtv heap failed");

    m_rtvStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_CPU_DESCRIPTOR_HANDLE h = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (int i = 0; i < 4; i++)
    {
        D3D12_RENDER_TARGET_VIEW_DESC v{};
        v.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        v.Format = (i==0?fmts.g0: i==1?fmts.g1: i==2?fmts.g2: fmts.g3);
        device->CreateRenderTargetView(m_gbuf[i].Get(), &v, h);
        h.ptr += m_rtvStride;
    }
}

void GBufferTargets::CreateDSV(ID3D12Device* device, const Formats& fmts)
{
    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    desc.NumDescriptors = 1;

    if (FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_dsvHeap))))
        throw std::runtime_error("gbuffer: create dsv heap failed");

    m_dsvStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

    D3D12_DEPTH_STENCIL_VIEW_DESC d{};
    d.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    d.Format = fmts.dsv;
    device->CreateDepthStencilView(m_depth.Get(), &d, m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
}

void GBufferTargets::CreateSRVs(ID3D12Device* device, const Formats& fmts)
{
    // 6 дескрипторов: g0..g3, depth, shadow (пока пуст)
    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = 6;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    if (FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_srvHeap))))
        throw std::runtime_error("gbuffer: create srv heap failed");

    m_srvStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_CPU_DESCRIPTOR_HANDLE h = m_srvHeap->GetCPUDescriptorHandleForHeapStart();

    for (int i = 0; i < 4; i++)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC s{};
        s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        s.Format = (i==0?fmts.g0: i==1?fmts.g1: i==2?fmts.g2: fmts.g3);
        s.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(m_gbuf[i].Get(), &s, h);
        h.ptr += m_srvStride;
    }
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC s{};
        s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        s.Format = DXGI_FORMAT_R32_FLOAT;
        s.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(m_depth.Get(), &s, h);
        h.ptr += m_srvStride;
    }
    // slot 5 (shadow) оставляем пустым — заполним позже через GetSrvCPUAt(5)
}

D3D12_CPU_DESCRIPTOR_HANDLE GBufferTargets::GetRTV(int i) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += i * m_rtvStride;
    return h;
}

D3D12_CPU_DESCRIPTOR_HANDLE GBufferTargets::GetSrvCPUAt(int idx) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += idx * m_srvStride;
    return h;
}
