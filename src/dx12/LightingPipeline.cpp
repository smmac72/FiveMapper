#include "LightingPipeline.h"
#include "ShaderUtils.h"
#include <stdexcept>

using Microsoft::WRL::ComPtr;
using namespace dx12;

void LightingPipeline::Initialize(ID3D12Device* device, ID3D12RootSignature* rs, DXGI_FORMAT backbufferFormat)
{
    auto vs = dx12::ReadFileToBlob(L"shaders/lighting_vs.cso");
    auto ps = dx12::ReadFileToBlob(L"shaders/lighting_ps.cso");

    // rasterizer for fullscreen tri
    D3D12_RASTERIZER_DESC rast{};
    rast.FillMode = D3D12_FILL_MODE_SOLID;
    rast.CullMode = D3D12_CULL_MODE_NONE;
    rast.FrontCounterClockwise = FALSE;
    rast.DepthClipEnable = TRUE;

    // blend: default opaque
    D3D12_BLEND_DESC blend{};
    blend.AlphaToCoverageEnable = FALSE;
    blend.IndependentBlendEnable = FALSE;
    auto& rt = blend.RenderTarget[0];
    rt.BlendEnable = FALSE;
    rt.LogicOpEnable = FALSE;
    rt.SrcBlend = D3D12_BLEND_ONE;
    rt.DestBlend = D3D12_BLEND_ZERO;
    rt.BlendOp = D3D12_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D12_BLEND_ONE;
    rt.DestBlendAlpha = D3D12_BLEND_ZERO;
    rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rt.LogicOp = D3D12_LOGIC_OP_NOOP;
    rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    // depth disabled for post-lighting
    D3D12_DEPTH_STENCIL_DESC ds{};
    ds.DepthEnable = FALSE;
    ds.StencilEnable = FALSE;
    ds.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
    d.pRootSignature = rs;
    d.InputLayout = { nullptr, 0 }; // no vertex buffer
    d.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    d.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    d.RasterizerState = rast;
    d.BlendState = blend;
    d.DepthStencilState = ds;
    d.DSVFormat = DXGI_FORMAT_UNKNOWN;

    d.SampleMask = UINT_MAX;
    d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.NumRenderTargets = 1;
    d.RTVFormats[0] = backbufferFormat;
    d.SampleDesc.Count = 1;

    if (FAILED(device->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&m_pso))))
    {
        throw std::runtime_error("lighting pso creation failed");
    }
}