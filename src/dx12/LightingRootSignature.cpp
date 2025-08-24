#include "LightingRootSignature.h"
#include <d3d12.h>
#include <stdexcept>
using namespace dx12;

void LightingRootSignature::Initialize(ID3D12Device* device)
{
    // SRV for shader input - gbuffer
    // t0 - albedo+mask || t1 - normal.xy + roughness
    // t2 - spec/metal/emissive // t3 - extra
    D3D12_DESCRIPTOR_RANGE1 range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 4;
    range.BaseShaderRegister = 0;
    range.RegisterSpace = 0;
    range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER1 params[3] = {};

    // CBV for camera (VS + PS)
    params[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
    params[0].Descriptor.ShaderRegister= 0;
    params[0].Descriptor.RegisterSpace = 0;

    // CBV for sun (PS)
    params[1].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].ShaderVisibility         = D3D12_SHADER_VISIBILITY_PIXEL;
    params[1].Descriptor.ShaderRegister= 1;
    params[1].Descriptor.RegisterSpace = 0;

    // Descriptor table for the SRV
    params[2].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].ShaderVisibility         = D3D12_SHADER_VISIBILITY_PIXEL;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges   = &range;

    // static samplers
    D3D12_STATIC_SAMPLER_DESC samplers[2] = {};

    // s0 - linear wrap
    samplers[0].Filter         = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[0].AddressU       = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].AddressV       = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].AddressW       = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].ShaderRegister = 0;
    samplers[0].RegisterSpace  = 0;
    samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // s1 - point clamp
    samplers[1].Filter         = D3D12_FILTER_MIN_MAG_MIP_POINT;
    samplers[1].AddressU       = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].AddressV       = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].AddressW       = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].ShaderRegister = 1;
    samplers[1].RegisterSpace  = 0;
    samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // create root signature
    // same as the g-buffer one
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.Version                      = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rsDesc.Desc_1_1.NumParameters       = _countof(params);
    rsDesc.Desc_1_1.pParameters         = params;
    rsDesc.Desc_1_1.NumStaticSamplers   = _countof(samplers);
    rsDesc.Desc_1_1.pStaticSamplers     = samplers;
    rsDesc.Desc_1_1.Flags               = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    Microsoft::WRL::ComPtr<ID3DBlob> blob, error;
    HRESULT hr = D3D12SerializeVersionedRootSignature(&rsDesc, &blob, &error);
    if (FAILED(hr))
    {
        if (error) OutputDebugStringA((char*)error->GetBufferPointer());
        throw std::runtime_error("Lighting RS serialization failed");
    }
    hr = device->CreateRootSignature(
        0, blob->GetBufferPointer(), blob->GetBufferSize(),
        IID_PPV_ARGS(&m_rootSig)
    );
    if (FAILED(hr))
        throw std::runtime_error("Lighting RS creation failed");
}