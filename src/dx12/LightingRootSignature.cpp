#include "LightingRootSignature.h"
#include <stdexcept>

using Microsoft::WRL::ComPtr;
using namespace dx12;

void LightingRootSignature::Initialize(ID3D12Device* device)
{
    // b0: camera (invVP + camPos)
    D3D12_ROOT_PARAMETER1 params[3]{};

    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[0].Descriptor.ShaderRegister = 0;

    // b1: sun/light params (+ lightVP etc.)
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[1].Descriptor.ShaderRegister = 1;

    // t0..t5: g0..g3, depth, shadow
    D3D12_DESCRIPTOR_RANGE1 range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 6;
    range.BaseShaderRegister = 0; // t0
    range.RegisterSpace = 0;
    range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE; // <-- было DATA_STATIC
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges = &range;

    // static samplers: s0 linear, s1 point, s2 comparison (for shadow)
    D3D12_STATIC_SAMPLER_DESC ss[3]{};

    ss[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    ss[0].AddressU = ss[0].AddressV = ss[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    ss[0].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    ss[0].ShaderRegister = 0;
    ss[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    ss[1].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    ss[1].AddressU = ss[1].AddressV = ss[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    ss[1].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    ss[1].ShaderRegister = 1;
    ss[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    ss[2].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    ss[2].AddressU = ss[2].AddressV = ss[2].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    ss[2].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    ss[2].ShaderRegister = 2; // SShadow
    ss[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    ss[2].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE; // outside shadowmap => fully lit


    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rs{};
    rs.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rs.Desc_1_1.NumParameters = _countof(params);
    rs.Desc_1_1.pParameters = params;
    rs.Desc_1_1.NumStaticSamplers = _countof(ss);
    rs.Desc_1_1.pStaticSamplers = ss;
    rs.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> blob, err;
    if (FAILED(D3D12SerializeVersionedRootSignature(&rs, &blob, &err)))
    {
        if (err) OutputDebugStringA((char*)err->GetBufferPointer());
        throw std::runtime_error("Lighting RS serialize failed");
    }
    if (FAILED(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&m_rootSig))))
    {
        throw std::runtime_error("Lighting RS create failed");
    }
}
