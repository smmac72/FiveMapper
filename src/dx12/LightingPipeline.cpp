#include "LightingPipeline.h"
#include "GBufferPipeline.h"   // for ReadFileToBlob
#include <stdexcept>
using namespace dx12;

// create the PSO for the lighting geometry pass
void LightingPipeline::Initialize(
    ID3D12Device*        device,
    ID3D12RootSignature* rootSig,
    DXGI_FORMAT          rtvFormat
) {
    // define vertex data layout (triangle pos only)
    struct V { float pos[2]; };
    static const D3D12_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(V,pos),
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    // put compiled vertex/pixel shaders into binary blobs
    auto vs = ReadFileToBlob(L"shaders/lighting_vs.cso");
    auto ps = ReadFileToBlob(L"shaders/lighting_ps.cso");

    // fill PSO descriptor
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d = {};
    d.pRootSignature        = rootSig;
    d.VS                    = { vs->GetBufferPointer(), vs->GetBufferSize() };
    d.PS                    = { ps->GetBufferPointer(), ps->GetBufferSize() };
    d.InputLayout           = { layout, _countof(layout) };
    d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.NumRenderTargets      = 1;
    d.RTVFormats[0]         = rtvFormat;
    d.DSVFormat             = DXGI_FORMAT_UNKNOWN;
    d.SampleDesc.Count      = 1;
    d.SampleMask            = UINT_MAX;

    // no blend
    ZeroMemory(&d.BlendState, sizeof(d.BlendState));
    d.BlendState.RenderTarget[0].BlendEnable = FALSE;
    d.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    // no depthstencil
    d.DepthStencilState.DepthEnable    = FALSE;
    d.DepthStencilState.StencilEnable  = FALSE;

    // rasterizer - fills primitives with color
    d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

    HRESULT hr = device->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&m_pso));
    if (FAILED(hr))
        throw std::runtime_error("Failed to create Lighting PSO");
}
