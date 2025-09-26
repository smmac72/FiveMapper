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
#include "dx12/ShadowMap.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;

struct Vertex { XMFLOAT3 pos; XMFLOAT3 nrm; XMFLOAT4 tan; XMFLOAT2 uv; };

struct CameraCBGeom { XMFLOAT4X4 viewProj; };
struct ObjectCB     { XMFLOAT4X4 world;    };

struct CameraCBLight
{
    XMFLOAT4X4 invViewProj;
    XMFLOAT3   camPosWS; float _pad;
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

static void CreateUploadBuffer(ID3D12Device* dev, UINT64 size, ID3D12Resource** out)
{
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC buf{};
    buf.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; buf.Width = size;
    buf.Height = 1; buf.DepthOrArraySize = 1; buf.MipLevels = 1;
    buf.SampleDesc.Count = 1; buf.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(dev->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buf,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(out))))
        throw std::runtime_error("upload buffer creation failed");
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int)
{
    auto cfg = WindowConfig::load();
    Window window(hInstance, cfg);
    if (!window.getHWND()) { MessageBoxW(nullptr, L"Error creating the window", L"Error", MB_OK); return -1; }

    const HWND hwnd = window.getHWND();
    const uint32_t width  = window.getClientWidth();
    const uint32_t height = window.getClientHeight();

    dx12::DeviceResources devRes(hwnd, width, height);
    devRes.Initialize(/*gpuValidation=*/true);

    dx12::GBufferTargets gbuf;
    dx12::GBufferTargets::Formats fmts{};
    gbuf.Initialize(devRes.GetDevice(), width, height, fmts);

    dx12::GBufferRootSignature gRS; gRS.Initialize(devRes.GetDevice());
    dx12::GBufferPipeline gPSO;
    DXGI_FORMAT mrt[4] = { fmts.g0, fmts.g1, fmts.g2, fmts.g3 };
    gPSO.Initialize(devRes.GetDevice(), gRS.Get(), mrt, fmts.dsv);

    dx12::LightingRootSignature lightRS; lightRS.Initialize(devRes.GetDevice());
    dx12::LightingPipeline       lightPSO;
    lightPSO.Initialize(devRes.GetDevice(), lightRS.Get(), DXGI_FORMAT_R8G8B8A8_UNORM);

    // Shadow map (2048x2048) + write its SRV into gbuffer heap slot t5
    dx12::ShadowMap shadow;
    const uint32_t shadowSize = 2048;
    shadow.Initialize(devRes.GetDevice(), shadowSize, shadowSize);
    // t5 = index 5 в heap gbuffer
    dx12::ShadowMap::CreateSRV(devRes.GetDevice(), shadow.GetResource(), gbuf.GetSrvCPUAt(5));

    // Command objects
    ComPtr<ID3D12CommandAllocator> cmdAlloc;
    ComPtr<ID3D12GraphicsCommandList> cmdList;
    devRes.GetDevice()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAlloc));
    devRes.GetDevice()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc.Get(), nullptr, IID_PPV_ARGS(&cmdList));
    cmdList->Close();

    // Geometry (cube)
    Vertex verts[24]{}; uint16_t idx[36]{};
    auto setFace = [&](int v, int ib, XMFLOAT3 n, XMFLOAT4 t, XMFLOAT3 p0,XMFLOAT3 p1,XMFLOAT3 p2,XMFLOAT3 p3)
    {
        verts[v+0]={p0,n,t,{0,0}}; verts[v+1]={p1,n,t,{1,0}}; verts[v+2]={p2,n,t,{1,1}}; verts[v+3]={p3,n,t,{0,1}};
        idx[ib+0]=uint16_t(v+0); idx[ib+1]=uint16_t(v+1); idx[ib+2]=uint16_t(v+2);
        idx[ib+3]=uint16_t(v+0); idx[ib+4]=uint16_t(v+2); idx[ib+5]=uint16_t(v+3);
    };
    const float s=0.5f;
    setFace(0,0,{0,1,0},{1,0,0,1},{-s, s,-s},{ s, s,-s},{ s, s, s},{-s, s, s});
    setFace(4,6,{0,-1,0},{-1,0,0,1},{-s,-s, s},{ s,-s, s},{ s,-s,-s},{-s,-s,-s});
    setFace(8,12,{1,0,0},{0,1,0,1},{ s,-s,-s},{ s,-s, s},{ s, s, s},{ s, s,-s});
    setFace(12,18,{-1,0,0},{0,-1,0,1},{-s,-s, s},{-s,-s,-s},{-s, s,-s},{-s, s, s});
    setFace(16,24,{0,0,1},{1,0,0,1},{-s,-s, s},{ s,-s, s},{ s, s, s},{-s, s, s});
    setFace(20,30,{0,0,-1},{1,0,0,1},{-s, s,-s},{ s, s,-s},{ s,-s,-s},{-s,-s,-s});

    ComPtr<ID3D12Resource> vb, ib;
    CreateUploadBuffer(devRes.GetDevice(), sizeof(verts), &vb);
    CreateUploadBuffer(devRes.GetDevice(), sizeof(idx),   &ib);
    void* p=nullptr; vb->Map(0, nullptr, &p); std::memcpy(p, verts, sizeof(verts)); vb->Unmap(0,nullptr);
    ib->Map(0, nullptr, &p); std::memcpy(p, idx, sizeof(idx)); ib->Unmap(0,nullptr);

    D3D12_VERTEX_BUFFER_VIEW vbv{ vb->GetGPUVirtualAddress(), sizeof(verts), sizeof(Vertex) };
    D3D12_INDEX_BUFFER_VIEW  ibv{ ib->GetGPUVirtualAddress(), sizeof(idx), DXGI_FORMAT_R16_UINT };

    // Constant buffers
    const UINT CBAlign = 256;
    ComPtr<ID3D12Resource> cbGeom;  CreateUploadBuffer(devRes.GetDevice(), CBAlign*2, &cbGeom);
    ComPtr<ID3D12Resource> cbLight; CreateUploadBuffer(devRes.GetDevice(), CBAlign*2, &cbLight);
    uint8_t* cbGeomBase=nullptr;  cbGeom->Map(0,nullptr,(void**)&cbGeomBase);
    uint8_t* cbLightBase=nullptr; cbLight->Map(0,nullptr,(void**)&cbLightBase);

    // Shadow constants (placed into SunCB)
    SunCB sun{};
    sun.intensity = 3.0f; sun.color = {1,1,1};
    sun.shadowBias = 0.002f;
    sun.shadowStrength = 0.9f;
    sun.shadowTexelSize = { 1.0f/float(shadowSize), 1.0f/float(shadowSize) };

    bool firstFrame=true;

    while (window.processMessages())
    {
        devRes.WaitForPreviousFrame();

        // Camera
        XMVECTOR eye = XMVectorSet(0.0f, -3.0f, 1.0f, 1.0f);
        XMVECTOR at  = XMVectorSet(0.0f,  0.0f, 1.0f, 1.0f);
        XMVECTOR up  = XMVectorSet(0.0f,  0.0f, 1.0f, 0.0f);

        float aspect = float(width)/float(height);
        XMMATRIX view = XMMatrixLookAtRH(eye, at, up);
        XMMATRIX proj = XMMatrixPerspectiveFovRH(XMConvertToRadians(60.0f), aspect, 0.1f, 100.0f);
        XMMATRIX viewProj = view * proj;
        XMMATRIX invViewProj = XMMatrixInverse(nullptr, viewProj);

        CameraCBGeom camG{}; XMStoreFloat4x4(&camG.viewProj, viewProj);
        std::memcpy(cbGeomBase + 0*CBAlign, &camG, sizeof(camG));

        ObjectCB obj{}; XMStoreFloat4x4(&obj.world, XMMatrixIdentity());
        std::memcpy(cbGeomBase + 1*CBAlign, &obj, sizeof(obj));

        CameraCBLight camL{};
        XMStoreFloat4x4(&camL.invViewProj, invViewProj);
        XMStoreFloat3(&camL.camPosWS, eye);
        std::memcpy(cbLightBase + 0*CBAlign, &camL, sizeof(camL));

        // Sun/light
        XMVECTOR ldir = XMVector3Normalize(XMVectorSet(+0.3f, +0.8f, +0.5f, 0.0f));
        XMStoreFloat3(&sun.dirWS, ldir);

        // light view/proj (простая ортографическая «коробка» вокруг сцены)
        XMVECTOR lightPos = XMVectorScale(-ldir, 5.0f); // чуть вдали по лучу
        XMMATRIX lview = XMMatrixLookAtRH(lightPos, XMVectorZero(), XMVectorSet(0,0,1,0));
        XMMATRIX lproj = XMMatrixOrthographicOffCenterRH(-3,3, -3,3, 0.1f, 20.0f);
        XMMATRIX lightVP = lview * lproj;
        XMStoreFloat4x4(&sun.lightVP, lightVP);

        std::memcpy(cbLightBase + 1*CBAlign, &sun, sizeof(sun));

        // record
        cmdAlloc->Reset();
        cmdList->Reset(cmdAlloc.Get(), nullptr);

        // ---------- SHADOW PASS ----------
        // state: shadow texture COMMON/PS_RESOURCE -> DEPTH_WRITE
        {
            D3D12_RESOURCE_BARRIER b{};
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition.pResource = shadow.GetResource();
            b.Transition.StateBefore = firstFrame ? D3D12_RESOURCE_STATE_COMMON : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            b.Transition.StateAfter  = D3D12_RESOURCE_STATE_DEPTH_WRITE;
            b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmdList->ResourceBarrier(1, &b);
        }

        // set shadow viewport/scissor
        D3D12_VIEWPORT vpShadow{ 0.0f, 0.0f, float(shadowSize), float(shadowSize), 0.0f, 1.0f };
        D3D12_RECT     scShadow{ 0, 0, LONG(shadowSize), LONG(shadowSize) };
        cmdList->RSSetViewports(1, &vpShadow);
        cmdList->RSSetScissorRects(1, &scShadow);

        // bind only DSV, clear
        auto dsvShadow = shadow.GetDSV();
        cmdList->OMSetRenderTargets(0, nullptr, FALSE, &dsvShadow);
        cmdList->ClearDepthStencilView(dsvShadow, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

        // use same RS/PSO as geometry? мы можем: RS гбуфера ок; но нужен depth-only PSO.
        // у нас нет отдельного shadow PSO: используем gbuffer PSO (он пишет MRT), это избыточно.
        // для краткости оставим gbuffer PSO — глубина всё равно запишется, RTV не привязан.
        cmdList->SetGraphicsRootSignature(gRS.Get());
        cmdList->SetPipelineState(gPSO.GetPSO());

        // но корбеффер b0 должен быть lightVP вместо cameraVP
        // трюк: временно зальём в тот же слот b0 матрицу lightVP (формально ок для теста)
        {
            CameraCBGeom lightCB{};
            XMStoreFloat4x4(&lightCB.viewProj, lightVP);
            std::memcpy(cbGeomBase + 0*CBAlign, &lightCB, sizeof(lightCB));
        }

        D3D12_GPU_VIRTUAL_ADDRESS b0 = cbGeom->GetGPUVirtualAddress() + 0*CBAlign;
        D3D12_GPU_VIRTUAL_ADDRESS b1 = cbGeom->GetGPUVirtualAddress() + 1*CBAlign;
        cmdList->SetGraphicsRootConstantBufferView(0, b0);
        cmdList->SetGraphicsRootConstantBufferView(1, b1);

        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmdList->IASetVertexBuffers(0, 1, &vbv);
        cmdList->IASetIndexBuffer(&ibv);
        cmdList->DrawIndexedInstanced(36, 1, 0, 0, 0);

        // restore camera VP back into cbGeom[0] for geometry pass
        {
            CameraCBGeom camRestore{}; XMStoreFloat4x4(&camRestore.viewProj, viewProj);
            std::memcpy(cbGeomBase + 0*CBAlign, &camRestore, sizeof(camRestore));
        }

        // transition shadow to PS_RESOURCE for lighting
        {
            D3D12_RESOURCE_BARRIER b{};
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition.pResource = shadow.GetResource();
            b.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
            b.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmdList->ResourceBarrier(1, &b);
        }

        // ---------- GEOMETRY (GBUFFER) ----------
        {
            D3D12_RESOURCE_BARRIER b[4]{};
            for (int i=0;i<4;i++)
            {
                b[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                b[i].Transition.pResource = gbuf.GetTex(i);
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

        D3D12_CPU_DESCRIPTOR_HANDLE mrt0 = gbuf.GetRTV(0);
        D3D12_CPU_DESCRIPTOR_HANDLE mrt1 = gbuf.GetRTV(1);
        D3D12_CPU_DESCRIPTOR_HANDLE mrt2 = gbuf.GetRTV(2);
        D3D12_CPU_DESCRIPTOR_HANDLE mrt3 = gbuf.GetRTV(3);
        D3D12_CPU_DESCRIPTOR_HANDLE dsv  = gbuf.GetDSV();
        D3D12_CPU_DESCRIPTOR_HANDLE mrt[4] = { mrt0,mrt1,mrt2,mrt3 };
        cmdList->OMSetRenderTargets(4, mrt, FALSE, &dsv);

        const float zero[4] = {0,0,0,1};
        for (int i=0;i<4;i++) cmdList->ClearRenderTargetView(mrt[i], zero, 0, nullptr);
        cmdList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

        D3D12_VIEWPORT vp{0,0,float(width),float(height),0,1};
        D3D12_RECT     sc{0,0,LONG(width),LONG(height)};
        cmdList->RSSetViewports(1, &vp);
        cmdList->RSSetScissorRects(1, &sc);

        cmdList->SetGraphicsRootSignature(gRS.Get());
        cmdList->SetPipelineState(gPSO.GetPSO());
        // camera VP (уже восстановлен)
        cmdList->SetGraphicsRootConstantBufferView(0, cbGeom->GetGPUVirtualAddress() + 0*CBAlign);
        cmdList->SetGraphicsRootConstantBufferView(1, cbGeom->GetGPUVirtualAddress() + 1*CBAlign);

        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmdList->IASetVertexBuffers(0,1,&vbv);
        cmdList->IASetIndexBuffer(&ibv);
        cmdList->DrawIndexedInstanced(36,1,0,0,0);

        // to PS_RESOURCE for lighting
        {
            D3D12_RESOURCE_BARRIER b[4]{};
            for (int i=0;i<4;i++)
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

        firstFrame=false;

        // ---------- LIGHTING ----------
        {
            // backbuffer present -> rt
            D3D12_RESOURCE_BARRIER bb{};
            bb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            bb.Transition.pResource = devRes.GetCurrentRT();
            bb.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            bb.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
            bb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmdList->ResourceBarrier(1, &bb);
        }

        D3D12_CPU_DESCRIPTOR_HANDLE bbRtv = devRes.GetCurrentRTV();
        const float clear[4] = {0.05f,0.05f,0.06f,1.0f};
        cmdList->OMSetRenderTargets(1, &bbRtv, FALSE, nullptr);
        cmdList->ClearRenderTargetView(bbRtv, clear, 0, nullptr);

        ID3D12DescriptorHeap* heaps[] = { gbuf.GetSrvHeap() };
        cmdList->SetDescriptorHeaps(1, heaps);

        cmdList->SetGraphicsRootSignature(lightRS.Get());
        cmdList->SetPipelineState(lightPSO.GetPSO());
        cmdList->SetGraphicsRootConstantBufferView(0, cbLight->GetGPUVirtualAddress() + 0*CBAlign);
        cmdList->SetGraphicsRootConstantBufferView(1, cbLight->GetGPUVirtualAddress() + 1*CBAlign);
        cmdList->SetGraphicsRootDescriptorTable(2, gbuf.GetSrvTableGPUStart());

        cmdList->RSSetViewports(1, &vp);
        cmdList->RSSetScissorRects(1, &sc);
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmdList->DrawInstanced(3,1,0,0);

        // RT -> Present
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
