#include "DeviceResources.h"
#include <stdexcept>
#include <array>
#include <d3d12sdklayers.h>
#include <iostream>
#include <assert.h>

using Microsoft::WRL::ComPtr;

DeviceResources::DeviceResources(HWND hwnd, uint32_t width, uint32_t height)
: m_hwnd(hwnd), m_width(width), m_height(height), m_frameIndex(0), m_fenceEvent(nullptr)
{

}

DeviceResources::~DeviceResources()
{
    WaitForGpu();
    if (m_fenceEvent)
    {
        CloseHandle(m_fenceEvent);
    }
}

void DeviceResources::Initialize(bool enableGpuValidation)
{
    // debug layer
    ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
    {
        debugController->EnableDebugLayer();
        if (enableGpuValidation)
        {
            ComPtr<ID3D12Debug1> debugController1;
            debugController.As(&debugController1);
            debugController1->SetEnableGPUBasedValidation(true);
        }
    }
    // factory
    CreateFactory(true);

    // device
    SelectAdapterAndCreateDevice();

    // queues
    CreateCommandQueues();

    // swapchain
    CreateSwapChain();

    // rtv heap and render targets
    CreateDescriptorHeaps();
    CreateRenderTargetViews();

    // fences
    CreateFences();

    // viewport and scissors
    CreateViewportScissor();
}

void DeviceResources::CreateFactory(bool enableDebug)
{
    UINT dxgiFlags = enableDebug ? DXGI_CREATE_FACTORY_DEBUG : 0;
    if (FAILED(CreateDXGIFactory2(dxgiFlags, IID_PPV_ARGS(&m_factory))))
    {
        throw std::runtime_error("Failed to create DXGI Factory");
    }
}

void DeviceResources::SelectAdapterAndCreateDevice()
{
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; m_factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++)
    {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
        {
            continue;
        }
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_device))))
        {
            break;
        }
        if (!m_device)
        {
            throw std::runtime_error("No GPU with DX12_0 support found");
        }
    }
}

void DeviceResources::CreateCommandQueues()
{
    D3D12_COMMAND_QUEUE_DESC desc = {};
    desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

    desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    m_device->CreateCommandQueue(&desc, IID_PPV_ARGS(&m_directQueue));
    
    desc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    m_device->CreateCommandQueue(&desc, IID_PPV_ARGS(&m_computeQueue));

    desc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
    m_device->CreateCommandQueue(&desc, IID_PPV_ARGS(&m_copyQueue));
}

void DeviceResources::CreateSwapChain()
{
    DXGI_SWAP_CHAIN_DESC1 scDesc = {};
    scDesc.BufferCount = kFrameCount;
    scDesc.Width = m_width;
    scDesc.Height = m_height;
    scDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scDesc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> swap1;
    m_factory->CreateSwapChainForHwnd(
        m_directQueue.Get(),
        m_hwnd,
        &scDesc,
        nullptr,
        nullptr,
        &swap1);
    
    swap1.As(&m_swapChain);
    m_factory->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER);
}

void DeviceResources::CreateDescriptorHeaps()
{
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = kFrameCount;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_rtvHeap));

    m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
}

void DeviceResources::CreateRenderTargetViews()
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (uint32_t i = 0; i < kFrameCount; i++)
    {
        m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i]));
        m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, handle);
        handle.ptr += m_rtvDescriptorSize;
    }
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void DeviceResources::CreateFences()
{
    for (uint32_t i = 0; i < kFrameCount; i++)
    {
        m_fenceValue[i] = 0;
        m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    }
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

void DeviceResources::CreateViewportScissor()
{
    m_viewport = { 0.0f, 0.0f,
                   static_cast<float>(m_width),
                   static_cast<float>(m_height),
                   0.0f, 1.0f };
    m_scissorRect = { 0, 0,
                      static_cast<LONG>(m_width),
                      static_cast<LONG>(m_height) };
}

D3D12_CPU_DESCRIPTOR_HANDLE DeviceResources::GetCurrentRTV() const
{
    auto h = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += m_frameIndex * m_rtvDescriptorSize;
    return h;
}

void DeviceResources::WaitForGpu() noexcept
{
    const uint64_t v = m_fenceValue[m_frameIndex]++;
    m_directQueue->Signal(m_fence.Get(), v);
    if (m_fence->GetCompletedValue() < v)
    {
        m_fence->SetEventOnCompletion(v, m_fenceEvent);
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
}

void DeviceResources::MoveToNextFrame() noexcept
{
    const uint64_t signal = m_fenceValue[m_frameIndex]++;
    m_directQueue->Signal(m_fence.Get(), signal);

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    if (m_fence->GetCompletedValue() < m_fenceValue[m_frameIndex])
    {
        m_fence->SetEventOnCompletion(m_fenceValue[m_frameIndex], m_fenceEvent);
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
}

// wait for gpu function but for the current frame index
void DeviceResources::WaitForPreviousFrame()
{
    const UINT64 currentValue = m_fenceValue[m_frameIndex];
    m_directQueue->Signal(m_fence.Get(), currentValue);

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    if (m_fence->GetCompletedValue() < m_fenceValue[m_frameIndex])
    {
        m_fence->SetEventOnCompletion(m_fenceValue[m_frameIndex], m_fenceEvent);
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
    m_fenceValue[m_frameIndex] = currentValue + 1;
}
