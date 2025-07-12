#pragma once
#include <wrl/client.h>
#include <d3d12.h>
#include <string>

namespace dx12
{

// helper to load a compiled shader into ID3DBlob
Microsoft::WRL::ComPtr<ID3DBlob> ReadFileToBlob(const std::wstring& path);

// encapsulates g-buffer PSO (geometry-pass)
class GBufferPipeline
{
public:
    void Initialize(
                    ID3D12Device* device,
                    ID3D12RootSignature* rootSignature,
                    DXGI_FORMAT rtvFormats[4], // multiple render target support - we do const 4 (albedo + occlusion, normal.xy + roughness, specular + emissive, other stuff)
                    DXGI_FORMAT dsvFormat);

    ID3D12PipelineState* GetPSO() const { return m_pso.Get(); }

private:
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pso;
};

}