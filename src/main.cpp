#include <windows.h>
#include <wrl/client.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <DirectXMath.h>

#include <stdexcept>
#include <cstdint>
#include <cstring>
#include <cstdio>

#include "platform/Window.h"
#include "dx12/DeviceResources.h"
#include "dx12/GBufferTargets.h"
#include "dx12/GBufferRootSignature.h"
#include "dx12/GBufferPipeline.h"
#include "dx12/LightingRootSignature.h"
#include "dx12/LightingPipeline.h"
#include "dx12/ShadowMap.h"
#include "dx12/ShadowRootSignature.h"
#include "dx12/ShadowPipeline.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;

// ----- vertex and cbuffer layouts (must match hlsl) -----
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

struct CameraCBLight
{
    XMFLOAT4X4 invViewProj;
    XMFLOAT3   camPosWS;
    float      _pad;
};

struct SunCB
{
    XMFLOAT3 dirWS;  float intensity;
    XMFLOAT3 color;  float _pad0;
    XMFLOAT4X4 lightVP;
    XMFLOAT2   shadowTexelSize;
    float      shadowBias;
    float      shadowStrength;
};

struct LightCBShadow
{
    XMFLOAT4X4 lightVP;
};

// ----- helpers -----
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

    if (FAILED(dev->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &buf,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(out))))
    {
        throw std::runtime_error("upload buffer creation failed");
    }
}

// high-res timer for dt
struct HiTimer
{
    LARGE_INTEGER freq{};
    LARGE_INTEGER last{};

    void init()
    {
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&last);
    }

    float tick()
    {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        double d = double(now.QuadPart - last.QuadPart) / double(freq.QuadPart);
        last = now;
        return float(d);
    }
};

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int)
{
    // window and config
    auto cfg = WindowConfig::load();
    Window window(hInstance, cfg);
    if (!window.getHWND())
    {
        MessageBoxW(nullptr, L"Error creating the window", L"Error", MB_OK);
        return -1;
    }

    const HWND hwnd  = window.getHWND();
    const uint32_t W = window.getClientWidth();
    const uint32_t H = window.getClientHeight();

    // device + swapchain
    dx12::DeviceResources devRes(hwnd, W, H);
    devRes.Initialize(/*gpuValidation=*/true);

    // gbuffer targets (rtv/srv/dsv heaps inside)
    dx12::GBufferTargets gbuf;
    dx12::GBufferTargets::Formats fmts{};
    gbuf.Initialize(devRes.GetDevice(), W, H, fmts);

    // gbuffer pso + rs
    dx12::GBufferRootSignature gRS; gRS.Initialize(devRes.GetDevice());
    dx12::GBufferPipeline gPSO;
    DXGI_FORMAT mrt[4] = { fmts.g0, fmts.g1, fmts.g2, fmts.g3 };
    gPSO.Initialize(devRes.GetDevice(), gRS.Get(), mrt, fmts.dsv);

    // lighting pso + rs
    dx12::LightingRootSignature lightRS; lightRS.Initialize(devRes.GetDevice());
    dx12::LightingPipeline lightPSO;
    lightPSO.Initialize(devRes.GetDevice(), lightRS.Get(), DXGI_FORMAT_R8G8B8A8_UNORM);

    // shadow map + pso + rs
    const uint32_t shadowSize = 2048;
    dx12::ShadowMap shadow; shadow.Initialize(devRes.GetDevice(), shadowSize, shadowSize);
    dx12::ShadowMap::CreateSRV(devRes.GetDevice(), shadow.GetResource(), gbuf.GetSrvCPUAt(5));

    dx12::ShadowRootSignature shRS; shRS.Initialize(devRes.GetDevice());
    dx12::ShadowPipeline shPSO; shPSO.Initialize(devRes.GetDevice(), shRS.Get(), DXGI_FORMAT_D32_FLOAT);

    // per-frame command allocators
    ComPtr<ID3D12CommandAllocator> cmdAlloc[dx12::DeviceResources::kFrameCount];
    for (UINT i = 0; i < dx12::DeviceResources::kFrameCount; ++i)
    {
        if (FAILED(devRes.GetDevice()->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAlloc[i]))))
        {
            throw std::runtime_error("CreateCommandAllocator failed");
        }
    }

    // single command list reused across frames
    ComPtr<ID3D12GraphicsCommandList> cmdList;
    {
        UINT fi = devRes.GetFrameIndex();
        if (FAILED(devRes.GetDevice()->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                cmdAlloc[fi].Get(), nullptr, IID_PPV_ARGS(&cmdList))))
        {
            throw std::runtime_error("CreateCommandList failed");
        }
        // close immediately so the first Reset() in the loop succeeds
        cmdList->Close();
    }
    bool listOpen = false; // book-keeping to avoid resetting an open list

    // simple unit cube
    Vertex verts[24]{};
    uint16_t idx[36]{};

    auto setFace = [&](int v, int ib,
                       XMFLOAT3 n, XMFLOAT4 t,
                       XMFLOAT3 p0, XMFLOAT3 p1, XMFLOAT3 p2, XMFLOAT3 p3)
    {
        verts[v + 0] = { p0, n, t, {0,0} };
        verts[v + 1] = { p1, n, t, {1,0} };
        verts[v + 2] = { p2, n, t, {1,1} };
        verts[v + 3] = { p3, n, t, {0,1} };

        idx[ib + 0] = uint16_t(v + 0);
        idx[ib + 1] = uint16_t(v + 1);
        idx[ib + 2] = uint16_t(v + 2);
        idx[ib + 3] = uint16_t(v + 0);
        idx[ib + 4] = uint16_t(v + 2);
        idx[ib + 5] = uint16_t(v + 3);
    };

    const float s = 0.5f;
    setFace(0, 0,   {0, 1, 0},  {1,0,0,1}, {-s, s,-s}, { s, s,-s}, { s, s, s}, {-s, s, s});
    setFace(4, 6,   {0,-1, 0}, {-1,0,0,1}, {-s,-s, s}, { s,-s, s}, { s,-s,-s}, {-s,-s,-s});
    setFace(8, 12,  {1, 0, 0},  {0,1,0,1}, { s,-s,-s}, { s,-s, s}, { s, s, s}, { s, s,-s});
    setFace(12, 18, {-1,0, 0},  {0,-1,0,1},{-s,-s, s}, {-s,-s,-s}, {-s, s,-s}, {-s, s, s});
    setFace(16, 24, {0, 0, 1},  {1,0,0,1}, {-s,-s, s}, { s,-s, s}, { s, s, s}, {-s, s, s});
    setFace(20, 30, {0, 0,-1},  {1,0,0,1}, {-s, s,-s}, { s, s,-s}, { s,-s,-s}, {-s,-s,-s});

    ComPtr<ID3D12Resource> vb, ib;
    CreateUploadBuffer(devRes.GetDevice(), sizeof(verts), &vb);
    CreateUploadBuffer(devRes.GetDevice(), sizeof(idx),   &ib);

    void* p = nullptr;
    vb->Map(0, nullptr, &p); std::memcpy(p, verts, sizeof(verts)); vb->Unmap(0, nullptr);
    ib->Map(0, nullptr, &p); std::memcpy(p, idx,   sizeof(idx));   ib->Unmap(0, nullptr);

    D3D12_VERTEX_BUFFER_VIEW vbv{ vb->GetGPUVirtualAddress(), UINT(sizeof(verts)), UINT(sizeof(Vertex)) };
    D3D12_INDEX_BUFFER_VIEW  ibv{ ib->GetGPUVirtualAddress(), UINT(sizeof(idx)),   DXGI_FORMAT_R16_UINT };

    // cbuffers (aligned to 256)
    const UINT CB = 256;
    ComPtr<ID3D12Resource> cbGeom;   CreateUploadBuffer(devRes.GetDevice(), CB * 2, &cbGeom);
    ComPtr<ID3D12Resource> cbLight;  CreateUploadBuffer(devRes.GetDevice(), CB * 2, &cbLight);
    ComPtr<ID3D12Resource> cbShadow; CreateUploadBuffer(devRes.GetDevice(), CB * 1, &cbShadow);

    uint8_t* cbGeomBase=nullptr;   cbGeom->Map(0,nullptr,(void**)&cbGeomBase);
    uint8_t* cbLightBase=nullptr;  cbLight->Map(0,nullptr,(void**)&cbLightBase);
    uint8_t* cbShadowBase=nullptr; cbShadow->Map(0,nullptr,(void**)&cbShadowBase);

    // camera state (z-up)
    XMFLOAT3 camPos = { 0.0f, -3.0f, 1.0f };
    float yaw   = XM_PIDIV2; // look toward +Y
    float pitch = 0.0f;
    const float mouseSens = 0.0025f;
    const float moveSpeed = 2.0f;

    HiTimer timer; timer.init();
    bool firstFrame = true;

    // main loop
    while (window.processMessages())
    {
        // begin frame guarantees gpu finished work for current frame index
        devRes.BeginFrame();
        UINT fi = devRes.GetFrameIndex();

        // defensive: if list stayed open due to earlier bug, close it now
        if (listOpen)
        {
            cmdList->Close();
            listOpen = false;
        }

        // reset allocator for this frame and bind it to the list
        cmdAlloc[fi]->Reset();
        cmdList->Reset(cmdAlloc[fi].Get(), nullptr);
        listOpen = true;

        // input: mouse look (raw)
        float dt = timer.tick();
        if (dt < 0.00001f) dt = 0.00001f;
        if (dt > 0.1f)     dt = 0.1f;

        POINT md = window.getMouseDelta();
        if (window.isActive() && window.isMouseCaptured() && (md.x != 0 || md.y != 0))
        {
            yaw   += mouseSens * float(md.x);
            pitch += mouseSens * float(-md.y);

            const float limit = 1.55f;
            if (pitch >  limit) pitch =  limit;
            if (pitch < -limit) pitch = -limit;
        }

        XMMATRIX rot = XMMatrixRotationRollPitchYaw(pitch, 0.0f, yaw);
        XMVECTOR F = XMVectorSet(0,1,0,0);
        XMVECTOR Rv= XMVectorSet(1,0,0,0);
        XMVECTOR U = XMVectorSet(0,0,1,0);
        XMVECTOR fwd   = XMVector3Normalize(XMVector3TransformNormal(F, rot));
        XMVECTOR right = XMVector3Normalize(XMVector3TransformNormal(Rv, rot));
        XMVECTOR up    = U;

        if (window.isActive())
        {
            float speed = moveSpeed * (window.isKeyDown(VK_SHIFT) ? 4.0f : 1.0f);
            XMVECTOR delta = XMVectorZero();
            if (window.isKeyDown('W')) delta = XMVectorAdd(delta, XMVectorScale(fwd,   speed * dt));
            if (window.isKeyDown('S')) delta = XMVectorAdd(delta, XMVectorScale(fwd,  -speed * dt));
            if (window.isKeyDown('D')) delta = XMVectorAdd(delta, XMVectorScale(right, speed * dt));
            if (window.isKeyDown('A')) delta = XMVectorAdd(delta, XMVectorScale(right,-speed * dt));
            if (window.isKeyDown('E')) delta = XMVectorAdd(delta, XMVectorSet(0,0, speed * dt,0));
            if (window.isKeyDown('Q')) delta = XMVectorAdd(delta, XMVectorSet(0,0,-speed * dt,0));

            XMFLOAT3 dMove{};
            XMStoreFloat3(&dMove, delta);
            camPos.x += dMove.x;
            camPos.y += dMove.y;
            camPos.z += dMove.z;
        }

        uint32_t curW = window.getClientWidth();
        uint32_t curH = window.getClientHeight();
        if (curW == 0) curW = 1;
        if (curH == 0) curH = 1;

        // camera matrices
        XMVECTOR eye = XMVectorSet(camPos.x, camPos.y, camPos.z, 1.0f);
        XMVECTOR at  = XMVectorAdd(eye, fwd);
        XMMATRIX view = XMMatrixLookAtRH(eye, at, up);
        float aspect = float(curW) / float(curH);
        XMMATRIX proj = XMMatrixPerspectiveFovRH(XMConvertToRadians(60.0f), aspect, 0.1f, 100.0f);
        XMMATRIX viewProj = view * proj;
        XMMATRIX invViewProj = XMMatrixInverse(nullptr, viewProj);

        // object/world
        ObjectCB obj{}; XMStoreFloat4x4(&obj.world, XMMatrixIdentity());
        std::memcpy(cbGeomBase + 1 * CB, &obj, sizeof(obj));

        // camera cb for geometry
        CameraCBGeom camG{}; XMStoreFloat4x4(&camG.viewProj, viewProj);
        std::memcpy(cbGeomBase + 0 * CB, &camG, sizeof(camG));

        // sun and lighting cb
        SunCB sun{};
        XMVECTOR ldir = XMVector3Normalize(XMVectorSet(+0.3f, +0.8f, +0.5f, 0.0f));
        XMStoreFloat3(&sun.dirWS, ldir);
        sun.intensity = 3.0f;
        sun.color = {1,1,1};
        sun.shadowBias = 0.002f;
        sun.shadowStrength = 0.9f;
        sun.shadowTexelSize = { 1.0f / float(shadowSize), 1.0f / float(shadowSize) };

        XMVECTOR lightPos = XMVectorScale(-ldir, 5.0f);
        XMMATRIX lview = XMMatrixLookAtRH(lightPos, XMVectorZero(), XMVectorSet(0,0,1,0));
        XMMATRIX lproj = XMMatrixOrthographicOffCenterRH(-3,3, -3,3, 0.1f, 20.0f);
        XMMATRIX lightVP = lview * lproj;
        XMStoreFloat4x4(&sun.lightVP, lightVP);

        CameraCBLight camL{}; XMStoreFloat4x4(&camL.invViewProj, invViewProj);
        camL.camPosWS = camPos;

        std::memcpy(cbLightBase + 0 * CB, &camL, sizeof(camL));
        std::memcpy(cbLightBase + 1 * CB, &sun,  sizeof(sun));

        LightCBShadow lcb{}; XMStoreFloat4x4(&lcb.lightVP, lightVP);
        std::memcpy(cbShadowBase + 0 * CB, &lcb, sizeof(lcb));

        // ----- shadow pass -----
        {
            D3D12_RESOURCE_BARRIER b{};
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition.pResource = shadow.GetResource();
            b.Transition.StateBefore = firstFrame ? D3D12_RESOURCE_STATE_COMMON
                                                  : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            b.Transition.StateAfter  = D3D12_RESOURCE_STATE_DEPTH_WRITE;
            b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmdList->ResourceBarrier(1, &b);
        }
        {
            D3D12_VIEWPORT vp{ 0, 0, float(curW), float(curH), 0, 1 };
            D3D12_RECT     sc{ 0, 0, LONG(curW), LONG(curH) };
            cmdList->RSSetViewports(1, &vp);
            cmdList->RSSetScissorRects(1, &sc);
        }
        {
            auto dsvShadow = shadow.GetDSV();
            cmdList->OMSetRenderTargets(0, nullptr, FALSE, &dsvShadow);
            cmdList->ClearDepthStencilView(dsvShadow, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        }
        cmdList->SetGraphicsRootSignature(shRS.Get());
        cmdList->SetPipelineState(shPSO.GetPSO());
        cmdList->SetGraphicsRootConstantBufferView(0, cbShadow->GetGPUVirtualAddress() + 0 * CB);
        cmdList->SetGraphicsRootConstantBufferView(1, cbGeom->GetGPUVirtualAddress()   + 1 * CB);

        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmdList->IASetVertexBuffers(0, 1, &vbv);
        cmdList->IASetIndexBuffer(&ibv);
        cmdList->DrawIndexedInstanced(36, 1, 0, 0, 0);

        {
            D3D12_RESOURCE_BARRIER b{};
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition.pResource = shadow.GetResource();
            b.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
            b.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmdList->ResourceBarrier(1, &b);
        }

        // ----- gbuffer pass -----
        {
            D3D12_RESOURCE_BARRIER b[4]{};
            for (int i = 0; i < 4; i++)
            {
                b[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                b[i].Transition.pResource   = gbuf.GetTex(i);
                b[i].Transition.StateBefore = firstFrame ? D3D12_RESOURCE_STATE_COMMON
                                                          : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                b[i].Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
                b[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            }
            cmdList->ResourceBarrier(4, b);
        }
        {
            D3D12_RESOURCE_BARRIER b{};
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition.pResource = gbuf.GetDepth();
            b.Transition.StateBefore = firstFrame ? D3D12_RESOURCE_STATE_COMMON
                                                  : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            b.Transition.StateAfter  = D3D12_RESOURCE_STATE_DEPTH_WRITE;
            b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmdList->ResourceBarrier(1, &b);
        }
        {
            D3D12_CPU_DESCRIPTOR_HANDLE mrt[4] = {
                gbuf.GetRTV(0), gbuf.GetRTV(1), gbuf.GetRTV(2), gbuf.GetRTV(3)
            };
            D3D12_CPU_DESCRIPTOR_HANDLE dsv = gbuf.GetDSV();
            cmdList->OMSetRenderTargets(4, mrt, FALSE, &dsv);

            const float zero[4] = { 0,0,0,1 };
            for (int i = 0; i < 4; ++i)
            {
                cmdList->ClearRenderTargetView(mrt[i], zero, 0, nullptr);
            }
            cmdList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        }
        {
            D3D12_VIEWPORT vp{ 0, 0, float(curW), float(curH), 0, 1 };
            D3D12_RECT     sc{ 0, 0, LONG(curW), LONG(curH) };
            cmdList->RSSetViewports(1, &vp);
            cmdList->RSSetScissorRects(1, &sc);
        }
        cmdList->SetGraphicsRootSignature(gRS.Get());
        cmdList->SetPipelineState(gPSO.GetPSO());
        cmdList->SetGraphicsRootConstantBufferView(0, cbGeom->GetGPUVirtualAddress() + 0 * CB);
        cmdList->SetGraphicsRootConstantBufferView(1, cbGeom->GetGPUVirtualAddress() + 1 * CB);

        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmdList->IASetVertexBuffers(0, 1, &vbv);
        cmdList->IASetIndexBuffer(&ibv);
        cmdList->DrawIndexedInstanced(36, 1, 0, 0, 0);

        {
            D3D12_RESOURCE_BARRIER b[4]{};
            for (int i = 0; i < 4; i++)
            {
                b[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                b[i].Transition.pResource = gbuf.GetTex(i);
                b[i].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
                b[i].Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                b[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            }
            cmdList->ResourceBarrier(4, b);
        }
        {
            D3D12_RESOURCE_BARRIER b{};
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition.pResource = gbuf.GetDepth();
            b.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
            b.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmdList->ResourceBarrier(1, &b);
        }

        firstFrame = false;

        // ----- lighting to backbuffer -----
        {
            D3D12_RESOURCE_BARRIER bb{};
            bb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            bb.Transition.pResource = devRes.GetCurrentRT();
            bb.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            bb.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
            bb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmdList->ResourceBarrier(1, &bb);
        }

        D3D12_CPU_DESCRIPTOR_HANDLE bbRtv = devRes.GetCurrentRTV();
        const float clear[4] = { 0.07f, 0.07f, 0.08f, 1.0f };
        cmdList->OMSetRenderTargets(1, &bbRtv, FALSE, nullptr);
        cmdList->ClearRenderTargetView(bbRtv, clear, 0, nullptr);

        ID3D12DescriptorHeap* heaps[] = { gbuf.GetSrvHeap() };
        cmdList->SetDescriptorHeaps(1, heaps);

        cmdList->SetGraphicsRootSignature(lightRS.Get());
        cmdList->SetPipelineState(lightPSO.GetPSO());
        cmdList->SetGraphicsRootConstantBufferView(0, cbLight->GetGPUVirtualAddress() + 0 * CB);
        cmdList->SetGraphicsRootConstantBufferView(1, cbLight->GetGPUVirtualAddress() + 1 * CB);
        cmdList->SetGraphicsRootDescriptorTable(2, gbuf.GetSrvTableGPUStart());

        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmdList->DrawInstanced(3, 1, 0, 0);

        {
            D3D12_RESOURCE_BARRIER bb{};
            bb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            bb.Transition.pResource = devRes.GetCurrentRT();
            bb.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            bb.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
            bb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmdList->ResourceBarrier(1, &bb);
        }

        // submit and present
        cmdList->Close();
        listOpen = false;

        ID3D12CommandList* lists[] = { cmdList.Get() };
        devRes.GetDirectQueue()->ExecuteCommandLists(1, lists);

        devRes.GetSwapChain()->Present(1, 0);

        // end frame: signal fence for this frame and advance frame index
        devRes.EndFrame();
    }

    devRes.WaitForGpu();
    cfg.save();
    return 0;
}
