#pragma once
#include <wrl/client.h>
#include <d3d12.h>
#include <dxgi1_6.h>

namespace dx12
{

// graphics pso for g-buffer geometry pass
class GBufferPipeline
{
public:
    void Initialize(
        ID3D12Device* device,
        ID3D12RootSignature* rootSig,
        const DXGI_FORMAT rtvFormats[4],
        DXGI_FORMAT dsvFormat
    );

    ID3D12PipelineState* GetPSO() const { return m_pso.Get(); }

private:
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pso;
};

}