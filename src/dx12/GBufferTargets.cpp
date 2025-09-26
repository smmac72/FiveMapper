#include "GBufferTargets.h"
#include <stdexcept>

using Microsoft::WRL::ComPtr;
using namespace dx12;

void GBufferTargets::Initialize(ID3D12Device* device, uint32_t width, uint32_t height, const Formats& fmts)
{
    // store sizes and formats
    m_width = width;
    m_height = height;
    m_formats = fmts;

    createHeaps(device);
    createResources(device, fmts);
}

void GBufferTargets::Shutdown()
{
    // release in reverse order to be explicit
    m_srvHeap.Reset();
    m_dsvHeap.Reset();
    m_rtvHeap.Reset();

    m_depth.Reset();

    for (int i = 4; i-- > 0; )
    {
        m_gbuf[i].Reset();
    }
}

void GBufferTargets::createHeaps(ID3D12Device* device)
{
    // rtv heap: 4 descriptors (cpu only)
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.NumDescriptors = 4;
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    if (FAILED(device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&m_rtvHeap))))
    {
        throw std::runtime_error("gbuffer rtv heap creation failed");
    }

    m_rtvStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // dsv heap: 1 descriptor (cpu only)
    D3D12_DESCRIPTOR_HEAP_DESC dsvDesc{};
    dsvDesc.NumDescriptors = 1;
    dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    if (FAILED(device->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&m_dsvHeap))))
    {
        throw std::runtime_error("gbuffer dsv heap creation failed");
    }

    // srv heap: 4 descriptors (shader visible)
    D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
    srvDesc.NumDescriptors = 4;
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    if (FAILED(device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&m_srvHeap))))
    {
        throw std::runtime_error("gbuffer srv heap creation failed");
    }

    m_srvStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void GBufferTargets::createResources(ID3D12Device* device, const Formats& fmts)
{
    // common texture description for 2d render targets
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC tex{};
    tex.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    tex.Width = m_width;
    tex.Height = m_height;
    tex.DepthOrArraySize = 1;
    tex.MipLevels = 1;
    tex.SampleDesc.Count = 1;
    tex.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    // create g0..g3 color targets
    const DXGI_FORMAT gFormats[4] =
    {
        fmts.g0, fmts.g1, fmts.g2, fmts.g3
    };

    for (int i = 0; i < 4; i++)
    {
        // we allow render target to write into this texture
        tex.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        tex.Format = gFormats[i];

        // clear value is not critical right now, but keep for correctness
        D3D12_CLEAR_VALUE clear{};
        clear.Format = gFormats[i];
        clear.Color[0] = 0.0f;
        clear.Color[1] = 0.0f;
        clear.Color[2] = 0.0f;
        clear.Color[3] = 0.0f;

        if (FAILED(device->CreateCommittedResource(
            &heap,
            D3D12_HEAP_FLAG_NONE,
            &tex,
            D3D12_RESOURCE_STATE_COMMON, // we will transition before writing
            &clear,
            IID_PPV_ARGS(&m_gbuf[i]))))
        {
            throw std::runtime_error("gbuffer color resource creation failed");
        }
    }

    // create rtv views for g0..g3
    for (int i = 0; i < 4; i++)
    {
        D3D12_RENDER_TARGET_VIEW_DESC rtv{};
        rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        rtv.Format = gFormats[i];
        rtv.Texture2D.MipSlice = 0;

        auto base = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        D3D12_CPU_DESCRIPTOR_HANDLE h{};
        h.ptr = base.ptr + SIZE_T(i) * m_rtvStride;

        device->CreateRenderTargetView(m_gbuf[i].Get(), &rtv, h);
    }

    // create srv views in shader-visible heap (for future lighting pass)
    for (int i = 0; i < 4; i++)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Format = gFormats[i];
        srv.Texture2D.MipLevels = 1;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

        auto base = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
        D3D12_CPU_DESCRIPTOR_HANDLE h{};
        h.ptr = base.ptr + SIZE_T(i) * m_srvStride;

        device->CreateShaderResourceView(m_gbuf[i].Get(), &srv, h);
    }

    // create depth target (dsv)
    tex.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    tex.Format = fmts.dsv;

    D3D12_CLEAR_VALUE dclear{};
    dclear.Format = fmts.dsv;
    dclear.DepthStencil.Depth = 1.0f;
    dclear.DepthStencil.Stencil = 0;

    if (FAILED(device->CreateCommittedResource(
        &heap,
        D3D12_HEAP_FLAG_NONE,
        &tex,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, // start ready for depth writing
        &dclear,
        IID_PPV_ARGS(&m_depth))))
    {
        throw std::runtime_error("gbuffer depth resource creation failed");
    }

    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
    dsv.Format = fmts.dsv;
    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

    device->CreateDepthStencilView(m_depth.Get(), &dsv, m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
}

D3D12_CPU_DESCRIPTOR_HANDLE GBufferTargets::GetRTV(size_t i) const
{
    auto base = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE h{};
    h.ptr = base.ptr + SIZE_T(i) * m_rtvStride;
    return h;
}

D3D12_CPU_DESCRIPTOR_HANDLE GBufferTargets::GetDSV() const
{
    return m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
}

D3D12_GPU_DESCRIPTOR_HANDLE GBufferTargets::GetSrvTableGPUStart() const
{
    return m_srvHeap->GetGPUDescriptorHandleForHeapStart();
}
