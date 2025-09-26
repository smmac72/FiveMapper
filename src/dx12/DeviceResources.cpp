#include "DeviceResources.h"
#include <d3d12sdklayers.h>
#include <stdexcept>
#include <string>

using Microsoft::WRL::ComPtr;
using namespace dx12;

static void LOGA(const char* s)  { OutputDebugStringA(s); }

DeviceResources::DeviceResources(HWND hwnd, uint32_t width, uint32_t height)
    : m_hwnd(hwnd), m_width(width), m_height(height)
{
}

DeviceResources::~DeviceResources()
{
    WaitForGpu();
    if (m_fenceEvent) CloseHandle(m_fenceEvent);
}

void DeviceResources::Initialize(bool enableGpuValidation)
{
    // 1 - debug layer
    if (enableGpuValidation)
    {
        ComPtr<ID3D12Debug> dbg;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg))))
        {
            dbg->EnableDebugLayer();
            ComPtr<ID3D12Debug1> dbg1;
            if (SUCCEEDED(dbg.As(&dbg1)))
            {
                dbg1->SetEnableGPUBasedValidation(TRUE);
            }
        }
    }

    // 2 - factory
    CreateFactory(enableGpuValidation);

    // 3 - device
    SelectAdapterAndCreateDevice();

    // 4 - info queue
    if (SUCCEEDED(m_device.As(&m_infoQueue)))
    {
        m_infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, FALSE);
        m_infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR,      FALSE);
        m_infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING,    FALSE);
    }

    // 5 - queue + swapchain + rtvs + fence + vp/scissor
    CreateCommandQueue();
    CreateSwapChain();
    CreateRTVHeapAndTargets();
    CreateFenceObjects();
    SetupViewportScissor();

    LOGA("[DR] Initialize complete\n");
}

void DeviceResources::CreateFactory(bool enableDebug)
{
    UINT flags = enableDebug ? DXGI_CREATE_FACTORY_DEBUG : 0;
    if (FAILED(CreateDXGIFactory2(flags, IID_PPV_ARGS(&m_factory))))
        throw std::runtime_error("CreateDXGIFactory2 failed");
}

void DeviceResources::SelectAdapterAndCreateDevice()
{
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; m_factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;

        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_device))))
        {
            LOGA("[DR] D3D12 device created\n");
            break;
        }
    }
    if (!m_device) throw std::runtime_error("No DX12_0 device found");
}

void DeviceResources::CreateCommandQueue()
{
    D3D12_COMMAND_QUEUE_DESC q{};
    q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(m_device->CreateCommandQueue(&q, IID_PPV_ARGS(&m_directQueue))))
        throw std::runtime_error("CreateCommandQueue failed");
}

void DeviceResources::CreateSwapChain()
{
    DXGI_SWAP_CHAIN_DESC1 sc{};
    sc.Width  = m_width;
    sc.Height = m_height;
    sc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sc.SampleDesc.Count = 1;
    sc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sc.BufferCount = kFrameCount;
    sc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    ComPtr<IDXGISwapChain1> sc1;
    if (FAILED(m_factory->CreateSwapChainForHwnd(
        m_directQueue.Get(), m_hwnd, &sc, nullptr, nullptr, &sc1)))
        throw std::runtime_error("CreateSwapChainForHwnd failed");

    if (FAILED(sc1.As(&m_swapChain)))
        throw std::runtime_error("SwapChain1->SwapChain3 cast failed");

    m_factory->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER);

    // текущий индекс back buffer (с этого начинаем кадр)
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void DeviceResources::CreateRTVHeapAndTargets()
{
    D3D12_DESCRIPTOR_HEAP_DESC h{};
    h.NumDescriptors = kFrameCount;
    h.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    h.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(m_device->CreateDescriptorHeap(&h, IID_PPV_ARGS(&m_rtvHeap))))
        throw std::runtime_error("CreateDescriptorHeap(RTV) failed");

    m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < kFrameCount; ++i)
    {
        if (FAILED(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i]))))
            throw std::runtime_error("SwapChain GetBuffer failed");
        m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, handle);
        handle.ptr += m_rtvDescriptorSize;
    }
}

void DeviceResources::CreateFenceObjects()
{
    if (FAILED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence))))
        throw std::runtime_error("CreateFence failed");

    // изначально все 0, GPU ещё ничего не исполнял
    for (UINT i = 0; i < kFrameCount; ++i) m_frameFenceValue[i] = 0;
    m_fenceLastSignaled = 0;

    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_fenceEvent) throw std::runtime_error("CreateEvent failed");
}

void DeviceResources::SetupViewportScissor()
{
    m_viewport = { 0.f, 0.f, static_cast<float>(m_width), static_cast<float>(m_height), 0.f, 1.f };
    m_scissorRect = { 0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height) };
}

D3D12_CPU_DESCRIPTOR_HANDLE DeviceResources::GetCurrentRTV() const
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += m_frameIndex * m_rtvDescriptorSize;
    return h;
}

// --- новая схема синхронизации ---

void DeviceResources::BeginFrame() noexcept
{
    // ждём fence, связанный с текущим frameIndex
    const UINT64 fenceToWait = m_frameFenceValue[m_frameIndex];
    if (m_fence->GetCompletedValue() < fenceToWait)
    {
        m_fence->SetEventOnCompletion(fenceToWait, m_fenceEvent);
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
}

void DeviceResources::EndFrame() noexcept
{
    // сигналим новый fence, привязываем его к ТЕКУЩЕМУ индексу
    const UINT64 fenceToSignal = ++m_fenceLastSignaled;
    m_directQueue->Signal(m_fence.Get(), fenceToSignal);
    m_frameFenceValue[m_frameIndex] = fenceToSignal;

    // после Present драйвер обновит индекс back buffer — читаем его
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void DeviceResources::WaitForGpu() noexcept
{
    // блокирующее ожидание (на выключение)
    const UINT64 fenceToSignal = ++m_fenceLastSignaled;
    m_directQueue->Signal(m_fence.Get(), fenceToSignal);
    m_fence->SetEventOnCompletion(fenceToSignal, m_fenceEvent);
    WaitForSingleObject(m_fenceEvent, INFINITE);
}
