#pragma once
#include <windows.h>
#include <wrl/client.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstdint>

namespace dx12
{

class DeviceResources
{
public:
    static constexpr uint32_t kFrameCount = 3;

    DeviceResources(HWND hwnd, uint32_t width, uint32_t height);
    ~DeviceResources();

    void Initialize(bool enableGpuValidation);

    // frame sync
    void WaitForPreviousFrame() noexcept;
    void WaitForGpu() noexcept;

    // getters
    ID3D12Device*           GetDevice()      const { return m_device.Get(); }
    IDXGISwapChain3*        GetSwapChain()   const { return m_swapChain.Get(); }
    ID3D12CommandQueue*     GetDirectQueue() const { return m_directQueue.Get(); }
    ID3D12Resource*         GetCurrentRT()   const { return m_renderTargets[m_frameIndex].Get(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRTV() const;

    uint32_t GetWidth()  const { return m_width; }
    uint32_t GetHeight() const { return m_height; }

private:
    void CreateFactory(bool enableDebug);
    void SelectAdapterAndCreateDevice();
    void CreateCommandQueue();
    void CreateSwapChain();
    void CreateRTVHeapAndTargets();
    void CreateFenceObjects();
    void SetupViewportScissor();

private:
    HWND m_hwnd = nullptr;
    uint32_t m_width = 0, m_height = 0;

    Microsoft::WRL::ComPtr<IDXGIFactory6>       m_factory;
    Microsoft::WRL::ComPtr<ID3D12Device>        m_device;
    Microsoft::WRL::ComPtr<ID3D12InfoQueue>     m_infoQueue;

    Microsoft::WRL::ComPtr<ID3D12CommandQueue>  m_directQueue;
    Microsoft::WRL::ComPtr<IDXGISwapChain3>     m_swapChain;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    UINT m_rtvDescriptorSize = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource>      m_renderTargets[kFrameCount];
    UINT                                         m_frameIndex = 0;

    Microsoft::WRL::ComPtr<ID3D12Fence>         m_fence;
    uint64_t                                     m_fenceValue[kFrameCount] = {};
    HANDLE                                       m_fenceEvent = nullptr;

    D3D12_VIEWPORT m_viewport{};
    D3D12_RECT     m_scissorRect{};
};

}
