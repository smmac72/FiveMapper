#pragma once
#include <wrl/client.h>
#include <d3d12.h>
#include <dxgi1_6.h>

namespace dx12
{

// pipeline state for fullscreen lighting pass
class LightingPipeline
{
public:
    void Initialize(ID3D12Device* device, ID3D12RootSignature* rs, DXGI_FORMAT backbufferFormat);
    ID3D12PipelineState* GetPSO() const { return m_pso.Get(); }

private:
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pso;
};

}