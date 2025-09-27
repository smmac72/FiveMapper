#pragma once
#include <wrl/client.h>
#include <d3d12.h>
#include <string>
#include <vector>

// note: minimal mesh loader using assimp. it flattens the first mesh of the file
// to a single vb/ib with position, normal, tangent, uv0. tangent is handedness-aware.

namespace assets
{

class Mesh
{
public:
    struct Vertex
    {
        float pos[3];
        float nrm[3];
        float tan[4];   // xyz + handedness
        float uv0[2];
    };

    bool LoadFromFile(const std::string& path);

    // gpu upload (simple: upload-heap buffers)
    void CreateBuffers(ID3D12Device* dev, ID3D12GraphicsCommandList* cmd);

    D3D12_VERTEX_BUFFER_VIEW GetVBV() const { return m_vbv; }
    D3D12_INDEX_BUFFER_VIEW  GetIBV() const { return m_ibv; }
    UINT GetIndexCount() const { return m_indexCount; }

private:
    std::vector<Vertex> m_vertices;
    std::vector<uint32_t> m_indices;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_vb;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_ib;
    D3D12_VERTEX_BUFFER_VIEW m_vbv{};
    D3D12_INDEX_BUFFER_VIEW  m_ibv{};
    UINT m_indexCount = 0;
};

} // namespace assets
