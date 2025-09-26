#include "ShadowRootSignature.h"
#include <stdexcept>
#include <d3d12.h>

using Microsoft::WRL::ComPtr;
using namespace dx12;

void ShadowRootSignature::Initialize(ID3D12Device* device)
{
    // b0: light view-projection (vertex)
    // b1: world matrix (vertex)
    D3D12_ROOT_PARAMETER1 params[2]{};

    params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
    params[0].Descriptor.ShaderRegister = 0;

    params[1].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
    params[1].Descriptor.ShaderRegister = 1;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rs{};
    rs.Version                 = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rs.Desc_1_1.NumParameters  = _countof(params);
    rs.Desc_1_1.pParameters    = params;
    rs.Desc_1_1.NumStaticSamplers = 0;
    rs.Desc_1_1.pStaticSamplers   = nullptr;
    rs.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> blob, err;
    if (FAILED(D3D12SerializeVersionedRootSignature(&rs, &blob, &err)))
    {
        if (err) OutputDebugStringA((char*)err->GetBufferPointer());
        throw std::runtime_error("shadow rs serialize failed");
    }
    if (FAILED(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                           IID_PPV_ARGS(&m_root))))
    {
        throw std::runtime_error("shadow rs create failed");
    }
}
