#include "LightingRootSignature.h"
#include <stdexcept>

using Microsoft::WRL::ComPtr;
using namespace dx12;

void LightingRootSignature::Initialize(ID3D12Device* device)
{
    // descriptor table for gbuffer srvs: t0..t4 (g0..g3, depth)
    D3D12_DESCRIPTOR_RANGE1 range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 5;           // was 4
    range.BaseShaderRegister = 0;       // t0
    range.RegisterSpace = 0;
    range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // root params:
    // p0: camera cbv (b0)  -- not used yet but keep for future pbr
    // p1: sun cbv    (b1)
    // p2: gbuffer table (t0..t3)
    D3D12_ROOT_PARAMETER1 params[3]{};

    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[0].Descriptor.ShaderRegister = 0; // b0
    params[0].Descriptor.RegisterSpace = 0;

    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[1].Descriptor.ShaderRegister = 1; // b1
    params[1].Descriptor.RegisterSpace = 0;

    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges = &range;

    // static samplers
    D3D12_STATIC_SAMPLER_DESC sams[3]{};

    // s0: linear wrap
    sams[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sams[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sams[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sams[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sams[0].MipLODBias = 0.0f;
    sams[0].MaxAnisotropy = 1;
    sams[0].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sams[0].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
    sams[0].MinLOD = 0.0f;
    sams[0].MaxLOD = D3D12_FLOAT32_MAX;
    sams[0].ShaderRegister = 0; // s0
    sams[0].RegisterSpace = 0;
    sams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // s1: point clamp
    sams[1].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sams[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sams[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sams[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sams[1].MipLODBias = 0.0f;
    sams[1].MaxAnisotropy = 1;
    sams[1].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sams[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
    sams[1].MinLOD = 0.0f;
    sams[1].MaxLOD = D3D12_FLOAT32_MAX;
    sams[1].ShaderRegister = 1; // s1
    sams[1].RegisterSpace = 0;
    sams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // s2: comparison linear clamp for future shadow sampling
    sams[2].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    sams[2].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sams[2].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sams[2].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sams[2].MipLODBias = 0.0f;
    sams[2].MaxAnisotropy = 1;
    sams[2].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    sams[2].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    sams[2].MinLOD = 0.0f;
    sams[2].MaxLOD = D3D12_FLOAT32_MAX;
    sams[2].ShaderRegister = 2; // s2
    sams[2].RegisterSpace = 0;
    sams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rs{};
    rs.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rs.Desc_1_1.NumParameters = _countof(params);
    rs.Desc_1_1.pParameters = params;
    rs.Desc_1_1.NumStaticSamplers = _countof(sams);
    rs.Desc_1_1.pStaticSamplers = sams;
    rs.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS
                      | D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS
                      | D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS
                      | D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    ComPtr<ID3DBlob> blob, err;
    HRESULT hr = D3D12SerializeVersionedRootSignature(&rs, &blob, &err);
    if (FAILED(hr))
    {
        if (err) OutputDebugStringA((const char*)err->GetBufferPointer());
        throw std::runtime_error("lighting root signature serialization failed");
    }

    hr = device->CreateRootSignature(
        0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&m_rootSig));
    if (FAILED(hr))
    {
        throw std::runtime_error("lighting root signature creation failed");
    }
}