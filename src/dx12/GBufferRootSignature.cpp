#include "GBufferRootSignature.h"
#include <d3d12.h>
#include <stdexcept>

using namespace dx12;

void GBufferRootSignature::Initialize(ID3D12Device* device)
{
    // texture slots
    // t0 - albedo+alpha/mask || t1 - normal || t2 - specular
    // t3 - emissive || t4 - displacement || t5 - detail albedo || t6 - detail normal
    D3D12_DESCRIPTOR_RANGE1 srvRange = {};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 7; // t0 to t6
    srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace = 0;
    srvRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // root parameters
    D3D12_ROOT_PARAMETER1 rootParams[4] = {};
    // CBV for camera matrices
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParams[0].Descriptor.RegisterSpace = 0;
    rootParams[0].Descriptor.ShaderRegister = 0;
    // CBV for object constants
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParams[1].Descriptor.RegisterSpace = 0;
    rootParams[1].Descriptor.ShaderRegister = 1;
    // CBV for global illumination
    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[2].Descriptor.RegisterSpace = 0;
    rootParams[2].Descriptor.ShaderRegister = 2;
    // Descriptor table for our texture slots
    rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[3].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[3].DescriptorTable.pDescriptorRanges = &srvRange;

    // static samplers
    D3D12_STATIC_SAMPLER_DESC staticSamplers[2] = {};
    // s0 - linear wrap
    staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].MipLODBias = 0;
    staticSamplers[0].MaxAnisotropy = 1;
    staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    staticSamplers[0].MinLOD = 0.0f;
    staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[0].ShaderRegister = 0;
    staticSamplers[0].RegisterSpace = 0;
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    // s1 - point clamp
    staticSamplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    staticSamplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[1].MipLODBias = 0;
    staticSamplers[1].MaxAnisotropy = 1;
    staticSamplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    staticSamplers[1].MinLOD = 0.0f;
    staticSamplers[1].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[1].ShaderRegister = 1;
    staticSamplers[1].RegisterSpace = 0;
    staticSamplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // create root signature
    // 1_1 supports some new flag stuff - i.e. D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC for unchanged tables (we use it)
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rsDesc.Desc_1_1.NumParameters = _countof(rootParams);
    rsDesc.Desc_1_1.pParameters = rootParams;
    rsDesc.Desc_1_1.NumStaticSamplers = _countof(staticSamplers);
    rsDesc.Desc_1_1.pStaticSamplers = staticSamplers;
    rsDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    // note to self - blob is a com-interface with a byte buffer
    // used for serializing root signature and storing other binary stuff like compiled shader byte-code
    Microsoft::WRL::ComPtr<ID3DBlob> blob, err;
    HRESULT hr = D3D12SerializeVersionedRootSignature(&rsDesc, &blob, &err);
    if (FAILED(hr))
    {
        if (err)
        {
            OutputDebugStringA((char*)err->GetBufferPointer());
        }
        throw std::runtime_error("Failed to serialize root signature");
    }

    hr = device->CreateRootSignature(
        0,
        blob->GetBufferPointer(),
        blob->GetBufferSize(),
        IID_PPV_ARGS(&m_rootSig)
    );
    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to create root signature");
    }
}