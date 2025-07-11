#pragma once

#include <wrl/client.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstdint>
#include <vector>

class DeviceResources
{
public:
    DeviceResources(HWND hwnd, uint32_t width, uint32_t height);
    ~DeviceResources();

    void Initialize(bool enableGpuValidation);

    void WaitForGpu() noexcept;

    void MoveToNextFrame() noexcept;

    void FlushDebugMessages() noexcept;

    void WaitForPreviousFrame();

    ID3D12Device* GetDevice() const { return m_device.Get(); }
    ID3D12CommandQueue* GetDirectQueue() const { return m_directQueue.Get(); }
    ID3D12CommandQueue* GetComputeQueue() const { return m_computeQueue.Get(); }
    ID3D12CommandQueue* GetCopyQueue() const { return m_copyQueue.Get(); }
    IDXGISwapChain3* GetSwapChain() const { return m_swapChain.Get(); }
    ID3D12Resource* GetCurrentRT() const { return m_renderTargets[m_swapChain->GetCurrentBackBufferIndex()].Get(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRTV() const;

    uint32_t GetWidth() const { return m_width; }
    uint32_t GetHeight() const { return m_height; }


private:
    void CreateFactory(bool enableDebug);
    void SelectAdapterAndCreateDevice();
    void CreateCommandQueues();
    void CreateSwapChain();
    void CreateDescriptorHeaps();
    void CreateRenderTargetViews();
    void CreateFences();

    void CreateViewportScissor();

private:
    HWND m_hwnd;
    uint32_t m_width;
    uint32_t m_height;

    Microsoft::WRL::ComPtr<IDXGIFactory4> m_factory;
    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
    
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_directQueue;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_computeQueue;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_copyQueue;

    Microsoft::WRL::ComPtr<IDXGISwapChain3> m_swapChain;
    static constexpr uint32_t kFrameCount = 3;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_renderTargets[kFrameCount];
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    uint32_t m_rtvDescriptorSize;
    uint32_t m_frameIndex;

    Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
    uint64_t m_fenceValue[kFrameCount];
    HANDLE m_fenceEvent;

    D3D12_VIEWPORT m_viewport;
    D3D12_RECT m_scissorRect;
};