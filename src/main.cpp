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

// ----- data structs sent to shaders (keep layout stable) -----
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

// ----- small helpers -----
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

// high resolution frame timer
struct HiTimer
{
    LARGE_INTEGER freq{};
    LARGE_INTEGER last{};

    void init()
    {
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&last);
    }

    // returns dt in seconds
    float tick()
    {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        const double d = double(now.QuadPart - last.QuadPart) / double(freq.QuadPart);
        last = now;
        return float(d);
    }
};

// ----- program entry -----
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

    const HWND hwnd  = window.getHWND();
    const uint32_t W = window.getClientWidth();
    const uint32_t H = window.getClientHeight();

    // dx12 device + swapchain
    dx12::DeviceResources devRes(hwnd, W, H);
    devRes.Initialize(/*gpuValidation=*/true);

    // gbuffer targets (rtv/srv/dsv heaps inside)
    dx12::GBufferTargets gbuf;
    dx12::GBufferTargets::Formats fmts{};
    gbuf.Initialize(devRes.GetDevice(), W, H, fmts);

    // gbuffer pipeline
    dx12::GBufferRootSignature gRS;
    gRS.Initialize(devRes.GetDevice());

    dx12::GBufferPipeline gPSO;
    DXGI_FORMAT mrt[4] = { fmts.g0, fmts.g1, fmts.g2, fmts.g3 };
    gPSO.Initialize(devRes.GetDevice(), gRS.Get(), mrt, fmts.dsv);

    // lighting pipeline
    dx12::LightingRootSignature lightRS;
    lightRS.Initialize(devRes.GetDevice());

    dx12::LightingPipeline lightPSO;
    lightPSO.Initialize(devRes.GetDevice(), lightRS.Get(), DXGI_FORMAT_R8G8B8A8_UNORM);

    // shadow map + pso
    dx12::ShadowMap shadow;
    const uint32_t shadowSize = 2048;
    shadow.Initialize(devRes.GetDevice(), shadowSize, shadowSize);

    // bind shadow map into gbuffer srv heap at slot t5
    dx12::ShadowMap::CreateSRV(devRes.GetDevice(), shadow.GetResource(), gbuf.GetSrvCPUAt(5));

    dx12::ShadowRootSignature shRS;
    shRS.Initialize(devRes.GetDevice());

    dx12::ShadowPipeline shPSO;
    shPSO.Initialize(devRes.GetDevice(), shRS.Get(), DXGI_FORMAT_D32_FLOAT);

    // command allocator/list
    ComPtr<ID3D12CommandAllocator>    cmdAlloc;
    ComPtr<ID3D12GraphicsCommandList> cmdList;
    devRes.GetDevice()->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAlloc));
    devRes.GetDevice()->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc.Get(), nullptr, IID_PPV_ARGS(&cmdList));
    cmdList->Close();

    // simple unit cube for testing
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

    // constant buffers (aligned to 256 bytes)
    const UINT CB = 256;
    ComPtr<ID3D12Resource> cbGeom;   CreateUploadBuffer(devRes.GetDevice(), CB * 2, &cbGeom);   // b0 camera, b1 world
    ComPtr<ID3D12Resource> cbLight;  CreateUploadBuffer(devRes.GetDevice(), CB * 2, &cbLight);  // b0 invVP+cam, b1 sun
    ComPtr<ID3D12Resource> cbShadow; CreateUploadBuffer(devRes.GetDevice(), CB * 1, &cbShadow); // b0 lightVP

    uint8_t* cbGeomBase=nullptr;   cbGeom->Map(0,nullptr,(void**)&cbGeomBase);
    uint8_t* cbLightBase=nullptr;  cbLight->Map(0,nullptr,(void**)&cbLightBase);
    uint8_t* cbShadowBase=nullptr; cbShadow->Map(0,nullptr,(void**)&cbShadowBase);

    // camera state (z-up)
    XMFLOAT3 camPos = { 0.0f, -3.0f, 1.0f };
    float yaw   = XM_PIDIV2; // look toward +Y to see cube
    float pitch = 0.0f;

    const float mouseSens = 0.0025f;
    const float moveSpeed = 2.0f; // m/s

    HiTimer timer; timer.init();
    float logAcc = 0.0f;
    bool firstFrame = true;

    // render loop
    while (window.processMessages())
    {
        // wait previous frame
        devRes.WaitForPreviousFrame();

        // dt
        float dt = timer.tick();
        if (dt < 0.00001f) dt = 0.00001f;
        if (dt > 0.1f)     dt = 0.1f;

        // mouse look (raw)
        POINT md = window.getMouseDelta();
        if (md.x != 0 || md.y != 0)
        {
            yaw   += mouseSens * float(md.x);
            pitch += mouseSens * float(-md.y);

            const float limit = 1.55f; // ~89 deg
            if (pitch >  limit) pitch =  limit;
            if (pitch < -limit) pitch = -limit;
        }

        // camera orientation via rotation matrix (z-up: pitch around X, yaw around Z)
        XMMATRIX rot = XMMatrixRotationRollPitchYaw(pitch, 0.0f, yaw);

        // base axes in camera-local
        XMVECTOR F = XMVectorSet(0, 1, 0, 0); // forward +Y
        XMVECTOR R = XMVectorSet(1, 0, 0, 0); // right +X
        XMVECTOR U = XMVectorSet(0, 0, 1, 0); // up +Z (world up)

        XMVECTOR fwd   = XMVector3Normalize(XMVector3TransformNormal(F, rot));
        XMVECTOR right = XMVector3Normalize(XMVector3TransformNormal(R, rot));
        XMVECTOR up    = U;

        // movement
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

        // debug print camera sometimes
        logAcc += dt;
        if (logAcc > 0.25f)
        {
            char buf[128];
            std::snprintf(buf, sizeof(buf),
                          "cam: %.2f %.2f %.2f  yaw=%.2f pitch=%.2f\n",
                          camPos.x, camPos.y, camPos.z, yaw, pitch);
            OutputDebugStringA(buf);
            logAcc = 0.0f;
        }

        // camera matrices
        XMVECTOR eye = XMVectorSet(camPos.x, camPos.y, camPos.z, 1.0f);
        XMVECTOR at  = XMVectorAdd(eye, fwd);
        XMMATRIX view = XMMatrixLookAtRH(eye, at, up);

        float aspect = float(W) / float(H);
        XMMATRIX proj = XMMatrixPerspectiveFovRH(XMConvertToRadians(60.0f), aspect, 0.1f, 100.0f);
        XMMATRIX viewProj = view * proj;
        XMMATRIX invViewProj = XMMatrixInverse(nullptr, viewProj);

        // object/world
        ObjectCB obj{}; XMStoreFloat4x4(&obj.world, XMMatrixIdentity());
        std::memcpy(cbGeomBase + 1 * CB, &obj, sizeof(obj));

        // camera cb for geometry
        CameraCBGeom camG{}; XMStoreFloat4x4(&camG.viewProj, viewProj);
        std::memcpy(cbGeomBase + 0 * CB, &camG, sizeof(camG));

        // sun + lighting camera cb
        SunCB sun{};
        XMVECTOR ldir = XMVector3Normalize(XMVectorSet(+0.3f, +0.8f, +0.5f, 0.0f));
        XMStoreFloat3(&sun.dirWS, ldir);
        sun.intensity = 3.0f;
        sun.color = {1,1,1};
        sun.shadowBias = 0.002f;
        sun.shadowStrength = 0.9f;
        sun.shadowTexelSize = { 1.0f / float(shadowSize), 1.0f / float(shadowSize) };

        // light matrices (simple ortho box around origin)
        XMVECTOR lightPos = XMVectorScale(-ldir, 5.0f);
        XMMATRIX lview = XMMatrixLookAtRH(lightPos, XMVectorZero(), XMVectorSet(0,0,1,0));
        XMMATRIX lproj = XMMatrixOrthographicOffCenterRH(-3,3, -3,3, 0.1f, 20.0f);
        XMMATRIX lightVP = lview * lproj;
        XMStoreFloat4x4(&sun.lightVP, lightVP);

        CameraCBLight camL{};
        XMStoreFloat4x4(&camL.invViewProj, invViewProj);
        camL.camPosWS = camPos;

        std::memcpy(cbLightBase + 0 * CB, &camL, sizeof(camL));
        std::memcpy(cbLightBase + 1 * CB, &sun,  sizeof(sun));

        LightCBShadow lcb{}; XMStoreFloat4x4(&lcb.lightVP, lightVP);
        std::memcpy(cbShadowBase + 0 * CB, &lcb, sizeof(lcb));

        // begin recording
        cmdAlloc->Reset();
        cmdList->Reset(cmdAlloc.Get(), nullptr);

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
            D3D12_VIEWPORT vps{ 0, 0, float(shadowSize), float(shadowSize), 0, 1 };
            D3D12_RECT     scs{ 0, 0, LONG(shadowSize), LONG(shadowSize) };
            cmdList->RSSetViewports(1, &vps);
            cmdList->RSSetScissorRects(1, &scs);
        }

        {
            auto dsvShadow = shadow.GetDSV();
            cmdList->OMSetRenderTargets(0, nullptr, FALSE, &dsvShadow);
            cmdList->ClearDepthStencilView(dsvShadow, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        }

        cmdList->SetGraphicsRootSignature(shRS.Get());
        cmdList->SetPipelineState(shPSO.GetPSO());
        cmdList->SetGraphicsRootConstantBufferView(0, cbShadow->GetGPUVirtualAddress() + 0 * CB); // lightVP
        cmdList->SetGraphicsRootConstantBufferView(1, cbGeom->GetGPUVirtualAddress()   + 1 * CB); // world

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
            D3D12_CPU_DESCRIPTOR_HANDLE mrt0 = gbuf.GetRTV(0);
            D3D12_CPU_DESCRIPTOR_HANDLE mrt1 = gbuf.GetRTV(1);
            D3D12_CPU_DESCRIPTOR_HANDLE mrt2 = gbuf.GetRTV(2);
            D3D12_CPU_DESCRIPTOR_HANDLE mrt3 = gbuf.GetRTV(3);
            D3D12_CPU_DESCRIPTOR_HANDLE dsv  = gbuf.GetDSV();
            D3D12_CPU_DESCRIPTOR_HANDLE mrtArr[4] = { mrt0, mrt1, mrt2, mrt3 };

            cmdList->OMSetRenderTargets(4, mrtArr, FALSE, &dsv);

            const float zero[4] = { 0,0,0,1 };
            for (int i = 0; i < 4; ++i)
            {
                cmdList->ClearRenderTargetView(mrtArr[i], zero, 0, nullptr);
            }
            cmdList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        }

        {
            D3D12_VIEWPORT vp{ 0, 0, float(W), float(H), 0, 1 };
            D3D12_RECT     sc{ 0, 0, LONG(W), LONG(H) };
            cmdList->RSSetViewports(1, &vp);
            cmdList->RSSetScissorRects(1, &sc);
        }

        cmdList->SetGraphicsRootSignature(gRS.Get());
        cmdList->SetPipelineState(gPSO.GetPSO());
        cmdList->SetGraphicsRootConstantBufferView(0, cbGeom->GetGPUVirtualAddress() + 0 * CB); // camera VP
        cmdList->SetGraphicsRootConstantBufferView(1, cbGeom->GetGPUVirtualAddress() + 1 * CB); // world

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

        // ----- lighting (to backbuffer) -----
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

        // small visual: background color reacts to camera pos
        const float clear[4] =
        {
            0.05f + 0.02f * camPos.x,
            0.05f + 0.02f * camPos.y,
            0.06f + 0.02f * camPos.z,
            1.0f
        };

        cmdList->OMSetRenderTargets(1, &bbRtv, FALSE, nullptr);
        cmdList->ClearRenderTargetView(bbRtv, clear, 0, nullptr);

        ID3D12DescriptorHeap* heaps[] = { gbuf.GetSrvHeap() };
        cmdList->SetDescriptorHeaps(1, heaps);

        cmdList->SetGraphicsRootSignature(lightRS.Get());
        cmdList->SetPipelineState(lightPSO.GetPSO());
        cmdList->SetGraphicsRootConstantBufferView(0, cbLight->GetGPUVirtualAddress() + 0 * CB);
        cmdList->SetGraphicsRootConstantBufferView(1, cbLight->GetGPUVirtualAddress() + 1 * CB);
        cmdList->SetGraphicsRootDescriptorTable(2, gbuf.GetSrvTableGPUStart());

        // fullscreen triangle (no vb)
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

        // submit + present (vsync on)
        cmdList->Close();
        ID3D12CommandList* lists[] = { cmdList.Get() };
        devRes.GetDirectQueue()->ExecuteCommandLists(1, lists);
        devRes.GetSwapChain()->Present(1, 0);
    }

    devRes.WaitForGpu();
    cfg.save();
    return 0;
}
