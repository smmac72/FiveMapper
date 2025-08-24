#pragma once
#include <wrl/client.h>
#include <d3d12.h>

namespace dx12 {

class LightingPipeline {
public:
    void Initialize(
        ID3D12Device*        device,
        ID3D12RootSignature* rootSig,
        DXGI_FORMAT          rtvFormat
    );
    ID3D12PipelineState* Get() const { return m_pso.Get(); }

private:
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pso;
};

}
