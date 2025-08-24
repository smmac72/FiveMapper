#include "GBufferTargets.h"
#include <stdexcept>
#include <cstring>

using Microsoft::WRL::ComPtr;
using namespace dx12;

void GBufferTargets::Initialize(ID3D12Device* device, uint32_t width, uint32_t height, const Formats& fmts)
{
    m_width = width; m_height = height; m_formats = fmts;
    createHeaps(device);
    createResources(device, fmts);
}

void GBufferTargets::Shutdown()
{
    for (auto& rt : m_gbuf) rt.Reset();
    m_depth.Reset();
    m_rtvHeap.Reset();
    m_dsvHeap.Reset();
    m_srvHeap.Reset();
}

void GBufferTargets::createHeaps(ID3D12Device* device)
{
    // RTV heap (4)
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
    rtvDesc.NumDescriptors = 4;
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&m_rtvHeap))))
        throw std::runtime_error("GBuffer RTV heap failed");
    m_rtvStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // DSV heap (1)
    D3D12_DESCRIPTOR_HEAP_DESC dsvDesc = {};
    dsvDesc.NumDescriptors = 1;
    dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(device->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&m_dsvHeap))))
        throw std::runtime_error("GBuffer DSV heap failed");

    // SRV heap (4), shader-visible
    D3D12_DESCRIPTOR_HEAP_DESC srvDesc = {};
    srvDesc.NumDescriptors = 4;
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&m_srvHeap))))
        throw std::runtime_error("GBuffer SRV heap failed");
    m_srvStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void GBufferTargets::createResources(ID3D12Device* device, const Formats& fmts)
{
    // Общие параметры 2D-RT
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC tex = {};
    tex.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    tex.Alignment = 0;
    tex.Width = m_width;
    tex.Height = m_height;
    tex.DepthOrArraySize = 1;
    tex.MipLevels = 1;
    tex.SampleDesc.Count = 1;
    tex.SampleDesc.Quality = 0;
    tex.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    // Clear values
    D3D12_CLEAR_VALUE clear = {};
    clear.Color[0] = 0; clear.Color[1] = 0; clear.Color[2] = 0; clear.Color[3] = 0;

    // --- G0..G3 (ALLOW_RENDER_TARGET), initial COMMON ---
    DXGI_FORMAT gFormats[4] = { fmts.g0, fmts.g1, fmts.g2, fmts.g3 };
    for (int i = 0; i < 4; ++i) {
        tex.Flags  = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        tex.Format = gFormats[i];
        clear.Format = gFormats[i];
        if (FAILED(device->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &tex, D3D12_RESOURCE_STATE_COMMON, &clear,
            IID_PPV_ARGS(&m_gbuf[i]))))
            throw std::runtime_error("GBuffer RT resource failed");
    }

    // RTVs
    for (int i = 0; i < 4; ++i) {
        D3D12_RENDER_TARGET_VIEW_DESC rtv = {};
        rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        rtv.Format = gFormats[i];
        rtv.Texture2D.MipSlice = 0;
        auto h = D3D12_CPU_DESCRIPTOR_HANDLE{ m_rtvHeap->GetCPUDescriptorHandleForHeapStart().ptr + i * m_rtvStride };
        device->CreateRenderTargetView(m_gbuf[i].Get(), &rtv, h);
    }

    // SRVs (shader-visible heap)
    for (int i = 0; i < 4; ++i) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Format = gFormats[i];
        srv.Texture2D.MipLevels = 1;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        auto h = D3D12_CPU_DESCRIPTOR_HANDLE{ m_srvHeap->GetCPUDescriptorHandleForHeapStart().ptr + i * m_srvStride };
        device->CreateShaderResourceView(m_gbuf[i].Get(), &srv, h);
    }

    // --- Depth (ALLOW_DEPTH_STENCIL), initial DEPTH_WRITE ---
    D3D12_CLEAR_VALUE dclear = {};
    dclear.Format = fmts.dsv;
    dclear.DepthStencil.Depth = 1.0f;
    dclear.DepthStencil.Stencil = 0;

    tex.Flags  = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    tex.Format = fmts.dsv;
    if (FAILED(device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &tex, D3D12_RESOURCE_STATE_DEPTH_WRITE, &dclear,
        IID_PPV_ARGS(&m_depth))))
        throw std::runtime_error("GBuffer depth resource failed");

    // DSV
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv = {};
    dsv.Format = fmts.dsv;
    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsv.Flags = 0;
    device->CreateDepthStencilView(m_depth.Get(), &dsv, m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
}
