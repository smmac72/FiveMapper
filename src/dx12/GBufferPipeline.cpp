#include "GBufferPipeline.h"
#include <d3dcompiler.h>
#include <fstream>
#include <vector>
#include <stdexcept>
#include <codecvt>

using Microsoft::WRL::ComPtr;
using namespace dx12;

// read shader file into d3dblob
ComPtr<ID3DBlob> dx12::ReadFileToBlob(const std::wstring& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        throw std::runtime_error("Failed to open shader file");
    }

    auto size = file.tellg();
    file.seekg(0);

    ComPtr<ID3DBlob> blob;
    HRESULT hr = D3DCreateBlob(size, &blob);
    if (FAILED(hr))
    {
        throw std::runtime_error("D3DCreateBlob failed");
    }

    if (!file.read(reinterpret_cast<char*>(blob->GetBufferPointer()), size))
    {
        throw std::runtime_error("Failed to read entire shader file");
    }
    return blob;
}

// create the PSO for the g-buffer geometry pass
void GBufferPipeline::Initialize(
                                ID3D12Device* device,
                                ID3D12RootSignature* rootSignature,
                                DXGI_FORMAT rtvFormats[4],
                                DXGI_FORMAT dsvFormat)
{
    struct Vertex { float pos[3], normal[3], tangent[3], uv[2]; };

    // define vertex data layout
    static const D3D12_INPUT_ELEMENT_DESC kInputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, pos), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, normal), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, tangent), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Vertex, uv), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    // put compiled vertex/pixel shaders into binary blobs
    auto vsBlob = ReadFileToBlob(L"shaders/gbuffer_vs.cso");
    auto psBlob = ReadFileToBlob(L"shaders/gbuffer_ps.cso");

    // fill PSO descriptor
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = rootSignature;
    desc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    desc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
    desc.InputLayout = { kInputLayout, _countof(kInputLayout) };
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 4; // multiple render targets amount
    for (UINT i = 0; i < 4; i++)
    {
        desc.RTVFormats[i] = rtvFormats[i];
    }
    desc.DSVFormat = dsvFormat;
    desc.SampleDesc.Count = 1;
    desc.SampleMask = UINT_MAX;

    // disable blending (mixing new color with existing in rtv) for g-buffer write-overs
    desc.BlendState.RenderTarget[0].BlendEnable = false;
    desc.BlendState.RenderTarget[1].BlendEnable = false;
    desc.BlendState.RenderTarget[2].BlendEnable = false;
    desc.BlendState.RenderTarget[3].BlendEnable = false;

    // depthstencil - mix of z-buffer and mask (pixel counter) to draw if something's drawn there
    desc.DepthStencilState.DepthEnable = true;
    desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    // rasterizer - builds and filters primitives
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; 
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    desc.RasterizerState.FrontCounterClockwise = false;

    // create PSO
    HRESULT hr = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&m_pso));
    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to create GBuffer PSO");
    }
}