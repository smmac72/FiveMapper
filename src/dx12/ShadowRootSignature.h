#pragma once
#include <wrl/client.h>
#include <d3d12.h>

namespace dx12
{

class ShadowRootSignature
{
public:
    void Initialize(ID3D12Device* device);

    ID3D12RootSignature* Get() const
    {
        return m_root.Get();
    }

private:
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_root;
};

}