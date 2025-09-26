#include "GBufferRootSignature.h"
#include <stdexcept>

using Microsoft::WRL::ComPtr;
using namespace dx12;

void GBufferRootSignature::Initialize(ID3D12Device* device)
{
    // descriptor table for up to 7 material textures (t0..t6)
    D3D12_DESCRIPTOR_RANGE1 srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 7;
    srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace = 0;
    // ВАЖНО: раньше было DATA_STATIC — меняем на DATA_STATIC_WHILE_SET
    srvRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // p0: camera cb (b0), p1: object cb (b1), p2: material srv table (t0..t6)
    D3D12_ROOT_PARAMETER1 params[3]{};

    // camera cbv (VS)
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    params[0].Descriptor.ShaderRegister = 0; // b0
    params[0].Descriptor.RegisterSpace = 0;

    // object cbv (ALL)
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].Descriptor.ShaderRegister = 1; // b1
    params[1].Descriptor.RegisterSpace = 0;

    // material textures table (PS)
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges = &srvRange;

    // static samplers
    D3D12_STATIC_SAMPLER_DESC sams[2]{};

    // s0: linear wrap
    sams[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sams[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sams[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sams[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sams[0].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sams[0].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
    sams[0].MinLOD = 0.0f;
    sams[0].MaxLOD = D3D12_FLOAT32_MAX;
    sams[0].ShaderRegister = 0;
    sams[0].RegisterSpace = 0;
    sams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // s1: point clamp
    sams[1].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sams[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sams[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sams[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sams[1].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sams[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
    sams[1].MinLOD = 0.0f;
    sams[1].MaxLOD = D3D12_FLOAT32_MAX;
    sams[1].ShaderRegister = 1;
    sams[1].RegisterSpace = 0;
    sams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // RS 1.1
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rs{};
    rs.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rs.Desc_1_1.NumParameters = _countof(params);
    rs.Desc_1_1.pParameters = params;
    rs.Desc_1_1.NumStaticSamplers = _countof(sams);
    rs.Desc_1_1.pStaticSamplers = sams;
    rs.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> blob, err;
    HRESULT hr = D3D12SerializeVersionedRootSignature(&rs, &blob, &err);
    if (FAILED(hr))
    {
        if (err) OutputDebugStringA((const char*)err->GetBufferPointer());
        throw std::runtime_error("gbuffer root signature serialization failed");
    }

    hr = device->CreateRootSignature(
        0, blob->GetBufferPointer(), blob->GetBufferSize(),
        IID_PPV_ARGS(&m_rootSig));
    if (FAILED(hr))
    {
        throw std::runtime_error("gbuffer root signature creation failed");
    }
}
