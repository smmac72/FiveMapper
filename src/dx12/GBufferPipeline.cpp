#include "GBufferPipeline.h"
#include "ShaderUtils.h"
#include <stdexcept>

using Microsoft::WRL::ComPtr;
using namespace dx12;

void GBufferPipeline::Initialize(
    ID3D12Device* device,
    ID3D12RootSignature* rootSig,
    const DXGI_FORMAT rtvFormats[4],
    DXGI_FORMAT dsvFormat
)
{
    // read compiled shaders (.cso)
    auto vs = dx12::ReadFileToBlob(L"shaders/gbuffer_vs.cso");
    auto ps = dx12::ReadFileToBlob(L"shaders/gbuffer_ps.cso");

    // input layout: position, normal, tangent, uv
    D3D12_INPUT_ELEMENT_DESC il[] =
    {
        // semantic, index, format, slot, offset, class, step-rate
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    // rasterizer state without d3dx12 helpers
    D3D12_RASTERIZER_DESC rast{};
    rast.FillMode = D3D12_FILL_MODE_SOLID;
    rast.CullMode = D3D12_CULL_MODE_NONE; // cull back faces for right-handed z-up
    rast.FrontCounterClockwise = FALSE;   // keep default winding
    rast.DepthBias = D3D12_DEFAULT_DEPTH_BIAS; // 0
    rast.DepthBiasClamp = 0.0f;
    rast.SlopeScaledDepthBias = 0.0f;
    rast.DepthClipEnable = TRUE;          // clip against near/far
    rast.MultisampleEnable = FALSE;
    rast.AntialiasedLineEnable = FALSE;
    rast.ForcedSampleCount = 0;
    rast.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    // blend state for 4 mrt, blending disabled
    D3D12_BLEND_DESC blend{};
    blend.AlphaToCoverageEnable = FALSE;
    blend.IndependentBlendEnable = FALSE;
    for (int i = 0; i < 8; i++)
    {
        auto& rt = blend.RenderTarget[i];
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
    }

    // depth-stencil: depth test on, write on, less-equal
    D3D12_DEPTH_STENCIL_DESC ds{};
    ds.DepthEnable = TRUE;
    ds.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    ds.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    ds.StencilEnable = FALSE;
    ds.StencilReadMask  = D3D12_DEFAULT_STENCIL_READ_MASK;   // 0xff
    ds.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;  // 0xff
    ds.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    ds.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    ds.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    ds.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    ds.BackFace = ds.FrontFace;

    // fill PSO
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
    d.pRootSignature = rootSig;
    d.InputLayout = { il, _countof(il) };
    d.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    d.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    d.RasterizerState = rast;
    d.BlendState = blend;
    d.DepthStencilState = ds;
    d.DSVFormat = dsvFormat;

    d.SampleMask = UINT_MAX;
    d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    d.NumRenderTargets = 4;
    d.RTVFormats[0] = rtvFormats[0];
    d.RTVFormats[1] = rtvFormats[1];
    d.RTVFormats[2] = rtvFormats[2];
    d.RTVFormats[3] = rtvFormats[3];

    d.SampleDesc.Count = 1;

    // create pso
    if (FAILED(device->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&m_pso))))
    {
        throw std::runtime_error("gbuffer pso creation failed");
    }
}
