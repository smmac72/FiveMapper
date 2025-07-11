#include <windows.h>
#include <wrl/client.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include "platform/Window.h"
#include "dx12/DeviceResources.h"

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

    // initialize DX12
    DeviceResources devRes(hwnd, width, height);
    devRes.Initialize(/*enableGpuValidation=*/true);

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

    // main loop
    while (window.processMessages())
    {
        // wait for the previous frame to be drawn
        devRes.WaitForPreviousFrame();

        cmdAlloc->Reset();
        cmdList->Reset(cmdAlloc.Get(), nullptr);

        // barrier Present -> RenderTarget
        {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrier.Transition.pResource   = devRes.GetCurrentRT();
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmdList->ResourceBarrier(1, &barrier);
        }

        // clear render target
        {
            auto hRTV = devRes.GetCurrentRTV();
            const float clearColor[4] = { 0.2f, 0.2f, 0.4f, 1.0f };
            cmdList->ClearRenderTargetView(hRTV, clearColor, 0, nullptr);
        }

        // TODO: here we render

        // barrier RenderTarget -> Present
        {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrier.Transition.pResource   = devRes.GetCurrentRT();
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmdList->ResourceBarrier(1, &barrier);
        }

        // close command list and execute
        cmdList->Close();
        ID3D12CommandList* lists[] = { cmdList.Get() };
        devRes.GetDirectQueue()->ExecuteCommandLists(_countof(lists), lists);

        // take from back buffer to front buffer
        // DXGI_SWAP_EFFECT_FLIP_DISCARD swapchain = rotates which buffer is the front one
        // no vsync, no allow tearing or other stuff in the second flag
        // that's a note for me, fuck that's hard to remember
        devRes.GetSwapChain()->Present(0, 0);
    }

    devRes.WaitForGpu();
    cfg.save();

    return 0;
}
