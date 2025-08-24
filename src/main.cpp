#include <windows.h>
#include <wrl/client.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include "platform/Window.h"
#include "dx12/DeviceResources.h"
#include "dx12/GBufferPipeline.h"
#include "dx12/GBufferRootSignature.h"
#include "dx12/GBufferTargets.h"
#include "dx12/LightingRootSignature.h"
#include "dx12/LightingPipeline.h"

using Microsoft::WRL::ComPtr;

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int)
{
    // load config
    auto cfg = WindowConfig::load();
    Window window(hInstance, cfg);
    if (!window.getHWND()) {
        MessageBoxW(nullptr, L"Error creating the window", L"Error", MB_OK);
        return -1;
    }

    HWND hwnd = window.getHWND();
    uint32_t width  = window.getClientWidth();
    uint32_t height = window.getClientHeight();

    // initialize DX12 device
    dx12::DeviceResources devRes(hwnd, width, height);
    devRes.Initialize(/*enableGpuValidation=*/true);

    // initialize g-buffer resources
    dx12::GBufferTargets gbuf;
    dx12::GBufferTargets::Formats fmts; // use predetermined formats
    gbuf.Initialize(devRes.GetDevice(), width, height, fmts);

    // initialize g-buffer root signature and PSO
    dx12::GBufferRootSignature gbufRS;
    gbufRS.Initialize(devRes.GetDevice());
    
    dx12::GBufferPipeline gbufPSO;
    DXGI_FORMAT rtvFormats[4] = { fmts.g0, fmts.g1, fmts.g2, fmts.g3 };
    gbufPSO.Initialize(devRes.GetDevice(), gbufRS.Get(), rtvFormats, fmts.dsv);

    // initialize lighting root signature and PSO
    dx12::LightingRootSignature lightingRS;
    lightingRS.Initialize(devRes.GetDevice());

    dx12::LightingPipeline lightingPSO;
    lightingPSO.Initialize(devRes.GetDevice(), lightingRS.Get(), DXGI_FORMAT_R8G8B8A8_UNORM);

    // command allocator and command list
    ComPtr<ID3D12CommandAllocator>    cmdAlloc;
    ComPtr<ID3D12GraphicsCommandList> cmdList;
    devRes.GetDevice()->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&cmdAlloc));
    devRes.GetDevice()->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        cmdAlloc.Get(),
        nullptr,
        IID_PPV_ARGS(&cmdList));
    cmdList->Close(); // close because opened initially

    // main render loop
    while (window.processMessages())
    {
        // wait for the previous frame to be drawn
        devRes.WaitForPreviousFrame();

        cmdAlloc->Reset();
        cmdList->Reset(cmdAlloc.Get(), nullptr);

        // ----- geometry pass ------
        // g-buffer: common -> render target
        {
            D3D12_RESOURCE_BARRIER b[4] = {};
            for (int i = 0; i < 4; i++)
            {
                b[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                b[i].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                b[i].Transition.pResource = gbuf.GetTex(i);
                b[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                b[i].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
                b[i].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            }
            cmdList->ResourceBarrier(4, b);
        }

        // attach descriptors to the frame
        {
            D3D12_CPU_DESCRIPTOR_HANDLE rt[4] = { gbuf.GetRTV(0), gbuf.GetRTV(1), gbuf.GetRTV(2), gbuf.GetRTV(3) };
            D3D12_CPU_DESCRIPTOR_HANDLE dsv = gbuf.GetDSV();
            cmdList->OMSetRenderTargets(4, rt, FALSE, &dsv);
        }

        // clear g-buffer + depthstencil
        {
            const float cz[4] = { 0, 0, 0, 1 };

            cmdList->ClearRenderTargetView(gbuf.GetRTV(0), cz, 0, nullptr);
            cmdList->ClearRenderTargetView(gbuf.GetRTV(1), cz, 0, nullptr);
            cmdList->ClearRenderTargetView(gbuf.GetRTV(2), cz, 0, nullptr);
            cmdList->ClearRenderTargetView(gbuf.GetRTV(3), cz, 0, nullptr);
            cmdList->ClearDepthStencilView(gbuf.GetDSV(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        }

        // bind rs/pso for g-buffer
        {
            cmdList->SetGraphicsRootSignature(gbufRS.Get());
            cmdList->SetPipelineState(gbufPSO.GetPSO());
        }

        // g-buffer: render target -> pixel shader (for lighting pass reading)
        {
            D3D12_RESOURCE_BARRIER b[4] = {};
            for (int i = 0; i < 4; i++)
            {
                b[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                b[i].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                b[i].Transition.pResource = gbuf.GetTex(i);
                b[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                b[i].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
                b[i].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            }
            cmdList->ResourceBarrier(4, b);
        }


        // ----- lighting pass ------
        // backbuffer: present -> render target
        {
            D3D12_RESOURCE_BARRIER bb = {};
            for (int i = 0; i < 4; i++)
            {
                bb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                bb.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                bb.Transition.pResource = gbuf.GetTex(i);
                bb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                bb.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
                bb.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            }
            cmdList->ResourceBarrier(1, &bb);
        }

        // attach descriptors to the frame
        {
            auto rtv = devRes.GetCurrentRTV();
            cmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        }

        // bind rs/pso for lighting
        {
            cmdList->SetGraphicsRootSignature(lightingRS.Get());
            cmdList->SetPipelineState(lightingPSO.Get());

            ID3D12DescriptorHeap* heaps[] = { gbuf.GetSrvHeap() };
            cmdList->SetDescriptorHeaps(1, heaps);

            cmdList->SetGraphicsRootDescriptorTable(2, gbuf.GetSrvTableGPUStart());
            cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            cmdList->DrawInstanced(3, 1, 0, 0);
        }

        // backbuffer: render target -> present
        {
            D3D12_RESOURCE_BARRIER bb = {};
            for (int i = 0; i < 4; i++)
            {
                bb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                bb.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                bb.Transition.pResource = gbuf.GetTex(i);
                bb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                bb.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
                bb.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
            }
            cmdList->ResourceBarrier(1, &bb);
        }

        // execute + present
        {
            cmdList->Close();
            ID3D12CommandList* lists[] = { cmdList.Get() };
            devRes.GetDirectQueue()->ExecuteCommandLists(_countof(lists), lists);

            // take from back buffer to front buffer
            // DXGI_SWAP_EFFECT_FLIP_DISCARD swapchain = rotates which buffer is the front one
            // no vsync, no allow tearing or other stuff in the second flag
            // that's a note for me, fuck that's hard to remember
            devRes.GetSwapChain()->Present(0, 0);
        }
    }

    devRes.WaitForGpu();
    cfg.save();

    return 0;
}
