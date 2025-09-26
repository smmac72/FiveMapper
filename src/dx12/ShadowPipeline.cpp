#include "ShadowPipeline.h"
#include "ShaderUtils.h"
#include <d3d12.h>
#include <stdexcept>
#include <fstream>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;
using namespace dx12;

void ShadowPipeline::Initialize(ID3D12Device* device,
                                ID3D12RootSignature* rs,
                                DXGI_FORMAT dsvFormat)
{
    // input layout matches our Vertex { float3 pos; float3 nrm; float4 tan; float2 uv; }
    D3D12_INPUT_ELEMENT_DESC il[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    ComPtr<ID3DBlob> vs = dx12::ReadFileToBlob(L"shaders/shadow_vs.cso");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
    d.pRootSignature = rs;
    d.InputLayout = { il, _countof(il) };
    d.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    d.PS = {}; // depth-only

    // depth-only: no color RTs
    d.NumRenderTargets = 0;
    d.DSVFormat = dsvFormat;

    // rasterizer with bias to reduce shadow acne
    d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    d.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT; // cull front to reduce peter-panning
    d.RasterizerState.DepthClipEnable = TRUE;
    d.RasterizerState.DepthBias = 100;                 // integer bias
    d.RasterizerState.SlopeScaledDepthBias = 1.5f;     // slope-scale bias
    d.RasterizerState.DepthBiasClamp = 0.0f;

    d.BlendState = D3D12_BLEND_DESC{};
    d.SampleMask = UINT_MAX;

    d.DepthStencilState.DepthEnable = TRUE;
    d.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    d.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    d.DepthStencilState.StencilEnable = FALSE;

    d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.SampleDesc.Count = 1;

    if (FAILED(device->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&m_pso))))
    {
        throw std::runtime_error("shadow pso create failed");
    }
}
