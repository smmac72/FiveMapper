#pragma once
#include <wrl/client.h>
#include <d3d12.h>
#include <string>

namespace dx12
{

class ShadowPipeline
{
public:
    void Initialize(ID3D12Device* device,
                    ID3D12RootSignature* rs,
                    DXGI_FORMAT dsvFormat);

    ID3D12PipelineState* GetPSO() const
    {
        return m_pso.Get();
    }

private:
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pso;
};

}