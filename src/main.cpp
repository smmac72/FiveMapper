#include <windows.h>
#include <wrl/client.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <DirectXMath.h>
#include <stdexcept>
#include <cstdint>
#include <cstring>

#include "platform/Window.h"
#include "dx12/DeviceResources.h"
#include "dx12/GBufferTargets.h"
#include "dx12/GBufferRootSignature.h"
#include "dx12/GBufferPipeline.h"
#include "dx12/LightingRootSignature.h"
#include "dx12/LightingPipeline.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;

struct Vertex
{
    XMFLOAT3 pos;
    XMFLOAT3 nrm;
    XMFLOAT4 tan;
    XMFLOAT2 uv;
};

struct CameraCBGeom
{
    XMFLOAT4X4 viewProj;
};

struct ObjectCB
{
    XMFLOAT4X4 world;
};

// lighting camera cb now holds invViewProj
struct CameraCBLight
{
    XMFLOAT4X4 invViewProj;
};

struct SunCB
{
    XMFLOAT3 dirWS;  float intensity;
    XMFLOAT3 color;  float _pad0;
};

static void CreateUploadBuffer(ID3D12Device* dev, UINT64 size, ID3D12Resource** out)
{
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC buf{};
    buf.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buf.Width = size;
    buf.Height = 1;
    buf.DepthOrArraySize = 1;
    buf.MipLevels = 1;
    buf.SampleDesc.Count = 1;
    buf.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    HRESULT hr = dev->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE,
        &buf,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(out)
    );
    if (FAILED(hr))
    {
        throw std::runtime_error("upload buffer creation failed");
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int)
{
    // window
    auto cfg = WindowConfig::load();
    Window window(hInstance, cfg);
    if (!window.getHWND())
    {
        MessageBoxW(nullptr, L"Error creating the window", L"Error", MB_OK);
        return -1;
    }

    const HWND hwnd = window.getHWND();
    const uint32_t width = window.getClientWidth();
    const uint32_t height = window.getClientHeight();

    // dx12 base
    dx12::DeviceResources devRes(hwnd, width, height);
    devRes.Initialize(/*enableGpuValidation=*/true);

    // gbuffer offscreen (now with depth SRV)
    dx12::GBufferTargets gbuf;
    dx12::GBufferTargets::Formats fmts{};
    gbuf.Initialize(devRes.GetDevice(), width, height, fmts);

    // geometry rs+pso
    dx12::GBufferRootSignature gRS;
    gRS.Initialize(devRes.GetDevice());

    dx12::GBufferPipeline gPSO;
    DXGI_FORMAT rtvFormats[4] = { fmts.g0, fmts.g1, fmts.g2, fmts.g3 };
    gPSO.Initialize(devRes.GetDevice(), gRS.Get(), rtvFormats, fmts.dsv);

    // lighting rs+pso
    dx12::LightingRootSignature lightRS;
    lightRS.Initialize(devRes.GetDevice());

    dx12::LightingPipeline lightPSO;
    lightPSO.Initialize(devRes.GetDevice(), lightRS.Get(), DXGI_FORMAT_R8G8B8A8_UNORM);

    // command objects
    ComPtr<ID3D12CommandAllocator>    cmdAlloc;
    ComPtr<ID3D12GraphicsCommandList> cmdList;
    devRes.GetDevice()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAlloc));
    devRes.GetDevice()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc.Get(), nullptr, IID_PPV_ARGS(&cmdList));
    cmdList->Close();

    // cube geometry
    Vertex verts[24]{};
    uint16_t idx[36]{};

    auto setFace =
        [&](int vbase, int ibase, XMFLOAT3 n, XMFLOAT4 t, XMFLOAT3 p0, XMFLOAT3 p1, XMFLOAT3 p2, XMFLOAT3 p3)
    {
        verts[vbase + 0] = { p0, n, t, {0,0} };
        verts[vbase + 1] = { p1, n, t, {1,0} };
        verts[vbase + 2] = { p2, n, t, {1,1} };
        verts[vbase + 3] = { p3, n, t, {0,1} };

        idx[ibase + 0] = uint16_t(vbase + 0);
        idx[ibase + 1] = uint16_t(vbase + 1);
        idx[ibase + 2] = uint16_t(vbase + 2);
        idx[ibase + 3] = uint16_t(vbase + 0);
        idx[ibase + 4] = uint16_t(vbase + 2);
        idx[ibase + 5] = uint16_t(vbase + 3);
    };

    const float s = 0.5f;
    setFace(0, 0, {0,1,0}, {1,0,0,1}, {-s, s,-s}, { s, s,-s}, { s, s, s}, {-s, s, s});
    setFace(4, 6, {0,-1,0}, {-1,0,0,1}, {-s,-s, s}, { s,-s, s}, { s,-s,-s}, {-s,-s,-s});
    setFace(8, 12, {1,0,0}, {0,1,0,1}, { s,-s,-s}, { s,-s, s}, { s, s, s}, { s, s,-s});
    setFace(12, 18, {-1,0,0}, {0,-1,0,1}, {-s,-s, s}, {-s,-s,-s}, {-s, s,-s}, {-s, s, s});
    setFace(16, 24, {0,0,1}, {1,0,0,1}, {-s,-s, s}, { s,-s, s}, { s, s, s}, {-s, s, s});
    setFace(20, 30, {0,0,-1}, {1,0,0,1}, {-s, s,-s}, { s, s,-s}, { s,-s,-s}, {-s,-s,-s});

    ComPtr<ID3D12Resource> vb, ib;
    CreateUploadBuffer(devRes.GetDevice(), sizeof(verts), &vb);
    CreateUploadBuffer(devRes.GetDevice(), sizeof(idx),   &ib);

    void* p = nullptr;
    vb->Map(0, nullptr, &p);
    std::memcpy(p, verts, sizeof(verts));
    vb->Unmap(0, nullptr);

    ib->Map(0, nullptr, &p);
    std::memcpy(p, idx, sizeof(idx));
    ib->Unmap(0, nullptr);

    D3D12_VERTEX_BUFFER_VIEW vbv{};
    vbv.BufferLocation = vb->GetGPUVirtualAddress();
    vbv.StrideInBytes = sizeof(Vertex);
    vbv.SizeInBytes = sizeof(verts);

    D3D12_INDEX_BUFFER_VIEW ibv{};
    ibv.BufferLocation = ib->GetGPUVirtualAddress();
    ibv.Format = DXGI_FORMAT_R16_UINT;
    ibv.SizeInBytes = sizeof(idx);

    // const buffers
    const UINT kCBAlign = 256u;

    // geometry: b0 camera, b1 object
    ComPtr<ID3D12Resource> cbGeom;
    CreateUploadBuffer(devRes.GetDevice(), kCBAlign * 2, &cbGeom);
    uint8_t* cbGeomBase = nullptr;
    cbGeom->Map(0, nullptr, reinterpret_cast<void**>(&cbGeomBase));

    // lighting: b0 camera(invViewProj), b1 sun
    ComPtr<ID3D12Resource> cbLight;
    CreateUploadBuffer(devRes.GetDevice(), kCBAlign * 2, &cbLight);
    uint8_t* cbLightBase = nullptr;
    cbLight->Map(0, nullptr, reinterpret_cast<void**>(&cbLightBase));

    bool firstFrame = true;

    while (window.processMessages())
    {
        devRes.WaitForPreviousFrame();

        // camera/object and lighting cb update
        XMMATRIX view, proj, viewProj, invViewProj;
        {
            XMVECTOR eye = XMVectorSet(0.0f, -3.0f, 1.0f, 1.0f);
            XMVECTOR at  = XMVectorSet(0.0f,  0.0f, 1.0f, 1.0f);
            XMVECTOR up  = XMVectorSet(0.0f,  0.0f, 1.0f, 0.0f);

            float aspect = float(width) / float(height);
            view = XMMatrixLookAtRH(eye, at, up);
            proj = XMMatrixPerspectiveFovRH(XMConvertToRadians(60.0f), aspect, 0.1f, 100.0f);
            viewProj = view * proj;
            invViewProj = XMMatrixInverse(nullptr, viewProj);

            CameraCBGeom cam{};
            XMStoreFloat4x4(&cam.viewProj, viewProj);

            ObjectCB obj{};
            XMStoreFloat4x4(&obj.world, XMMatrixIdentity());

            std::memcpy(cbGeomBase + 0 * kCBAlign, &cam, sizeof(CameraCBGeom));
            std::memcpy(cbGeomBase + 1 * kCBAlign, &obj, sizeof(ObjectCB));

            CameraCBLight camL{};
            XMStoreFloat4x4(&camL.invViewProj, invViewProj);

            SunCB sun{};
            XMVECTOR ldir = XMVector3Normalize(XMVectorSet(+0.3f, +0.8f, +0.5f, 0.0f));
            XMStoreFloat3(&sun.dirWS, ldir);
            sun.intensity = 3.0f;
            sun.color = { 1.0f, 1.0f, 1.0f };

            std::memcpy(cbLightBase + 0 * kCBAlign, &camL, sizeof(CameraCBLight));
            std::memcpy(cbLightBase + 1 * kCBAlign, &sun, sizeof(SunCB));
        }

        cmdAlloc->Reset();
        cmdList->Reset(cmdAlloc.Get(), nullptr);

        // gbuffer color: PS_RESOURCE/COMMON -> RENDER_TARGET
        {
            D3D12_RESOURCE_BARRIER b[4]{};
            for (int i = 0; i < 4; i++)
            {
                b[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                b[i].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                b[i].Transition.pResource = gbuf.GetTex(i);
                b[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                b[i].Transition.StateBefore = firstFrame
                    ? D3D12_RESOURCE_STATE_COMMON
                    : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                b[i].Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
            }
            cmdList->ResourceBarrier(4, b);
        }

        // depth: COMMON/PS_RESOURCE -> DEPTH_WRITE
        {
            D3D12_RESOURCE_BARRIER b{};
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            b.Transition.pResource = gbuf.GetDepth();
            b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            b.Transition.StateBefore = firstFrame
                ? D3D12_RESOURCE_STATE_COMMON
                : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            b.Transition.StateAfter  = D3D12_RESOURCE_STATE_DEPTH_WRITE;
            cmdList->ResourceBarrier(1, &b);
        }

        // bind mrt + dsv and clear
        D3D12_CPU_DESCRIPTOR_HANDLE mrt[4] =
        {
            gbuf.GetRTV(0), gbuf.GetRTV(1), gbuf.GetRTV(2), gbuf.GetRTV(3)
        };
        D3D12_CPU_DESCRIPTOR_HANDLE dsv = gbuf.GetDSV();

        cmdList->OMSetRenderTargets(4, mrt, FALSE, &dsv);

        const float zero[4] = { 0.f, 0.f, 0.f, 1.f };
        for (int i = 0; i < 4; i++)
        {
            cmdList->ClearRenderTargetView(mrt[i], zero, 0, nullptr);
        }
        cmdList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

        // viewport/scissor
        D3D12_VIEWPORT vp{};
        vp.TopLeftX = 0.0f;
        vp.TopLeftY = 0.0f;
        vp.Width = float(width);
        vp.Height = float(height);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;

        D3D12_RECT sc{};
        sc.left = 0; sc.top = 0; sc.right = LONG(width); sc.bottom = LONG(height);

        cmdList->RSSetViewports(1, &vp);
        cmdList->RSSetScissorRects(1, &sc);

        // geometry draw
        cmdList->SetGraphicsRootSignature(gRS.Get());
        cmdList->SetPipelineState(gPSO.GetPSO());

        D3D12_GPU_VIRTUAL_ADDRESS camAddr = cbGeom->GetGPUVirtualAddress() + 0 * kCBAlign;
        D3D12_GPU_VIRTUAL_ADDRESS objAddr = cbGeom->GetGPUVirtualAddress() + 1 * kCBAlign;
        cmdList->SetGraphicsRootConstantBufferView(0, camAddr);
        cmdList->SetGraphicsRootConstantBufferView(1, objAddr);

        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmdList->IASetVertexBuffers(0, 1, &vbv);
        cmdList->IASetIndexBuffer(&ibv);
        cmdList->DrawIndexedInstanced(36, 1, 0, 0, 0);

        // color gbuffer: RENDER_TARGET -> PS_RESOURCE
        {
            D3D12_RESOURCE_BARRIER b[4]{};
            for (int i = 0; i < 4; i++)
            {
                b[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                b[i].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                b[i].Transition.pResource = gbuf.GetTex(i);
                b[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                b[i].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
                b[i].Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            }
            cmdList->ResourceBarrier(4, b);
        }

        // depth: DEPTH_WRITE -> PS_RESOURCE
        {
            D3D12_RESOURCE_BARRIER b{};
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            b.Transition.pResource = gbuf.GetDepth();
            b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            b.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
            b.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            cmdList->ResourceBarrier(1, &b);
        }

        firstFrame = false;

        // backbuffer present -> render target
        {
            D3D12_RESOURCE_BARRIER bb{};
            bb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            bb.Transition.pResource = devRes.GetCurrentRT();
            bb.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            bb.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
            bb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmdList->ResourceBarrier(1, &bb);
        }

        // clear backbuffer
        D3D12_CPU_DESCRIPTOR_HANDLE bbRtv = devRes.GetCurrentRTV();
        const float clear[4] = { 0.05f, 0.05f, 0.06f, 1.0f };
        cmdList->OMSetRenderTargets(1, &bbRtv, FALSE, nullptr);
        cmdList->ClearRenderTargetView(bbRtv, clear, 0, nullptr);

        // lighting: bind srv heap (g0..g3, depth at t4)
        {
            ID3D12DescriptorHeap* heaps[] = { gbuf.GetSrvHeap() };
            cmdList->SetDescriptorHeaps(1, heaps);

            cmdList->SetGraphicsRootSignature(lightRS.Get());
            cmdList->SetPipelineState(lightPSO.GetPSO());

            D3D12_GPU_VIRTUAL_ADDRESS camL = cbLight->GetGPUVirtualAddress() + 0 * kCBAlign;
            D3D12_GPU_VIRTUAL_ADDRESS sunL = cbLight->GetGPUVirtualAddress() + 1 * kCBAlign;
            cmdList->SetGraphicsRootConstantBufferView(0, camL);
            cmdList->SetGraphicsRootConstantBufferView(1, sunL);

            cmdList->SetGraphicsRootDescriptorTable(2, gbuf.GetSrvTableGPUStart());

            cmdList->RSSetViewports(1, &vp);
            cmdList->RSSetScissorRects(1, &sc);
            cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            cmdList->DrawInstanced(3, 1, 0, 0);
        }

        // backbuffer render target -> present
        {
            D3D12_RESOURCE_BARRIER bb{};
            bb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            bb.Transition.pResource = devRes.GetCurrentRT();
            bb.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            bb.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
            bb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmdList->ResourceBarrier(1, &bb);
        }

        cmdList->Close();
        ID3D12CommandList* lists[] = { cmdList.Get() };
        devRes.GetDirectQueue()->ExecuteCommandLists(1, lists);
        devRes.GetSwapChain()->Present(1, 0);
    }

    devRes.WaitForGpu();
    cfg.save();
    return 0;
}
