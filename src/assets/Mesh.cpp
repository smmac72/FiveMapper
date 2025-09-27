#include "Mesh.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <stdexcept>
#include <cstring>

using Microsoft::WRL::ComPtr;

namespace assets
{

static ComPtr<ID3D12Resource> CreateUpload(ID3D12Device* dev, UINT64 size)
{
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC buf{};
    buf.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buf.Width = size; buf.Height = 1; buf.DepthOrArraySize = 1; buf.MipLevels = 1;
    buf.SampleDesc.Count = 1; buf.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> res;
    if (FAILED(dev->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buf,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&res))))
        throw std::runtime_error("mesh upload buffer creation failed");
    return res;
}

bool Mesh::LoadFromFile(const std::string& path)
{
    Assimp::Importer imp;
    unsigned flags =
        aiProcess_Triangulate |
        aiProcess_CalcTangentSpace |
        aiProcess_GenSmoothNormals |
        aiProcess_JoinIdenticalVertices |
        aiProcess_ImproveCacheLocality |
        aiProcess_SortByPType |
        aiProcess_OptimizeMeshes |
        aiProcess_FlipWindingOrder; // d3d default front is clockwise=false, our cube data is ccw; flipping here is safer

    const aiScene* sc = imp.ReadFile(path, flags);
    if (!sc || !sc->HasMeshes()) return false;

    const aiMesh* m = sc->mMeshes[0];

    m_vertices.resize(m->mNumVertices);
    for (unsigned i = 0; i < m->mNumVertices; ++i)
    {
        auto& v = m_vertices[i];
        aiVector3D p = m->mVertices[i];
        aiVector3D n = m->mNormals ? m->mNormals[i] : aiVector3D(0,0,1);
        aiVector3D t = m->mTangents ? m->mTangents[i] : aiVector3D(1,0,0);
        aiVector3D b = m->mBitangents ? m->mBitangents[i] : aiVector3D(0,1,0);
        aiVector3D uv = m->HasTextureCoords(0) ? m->mTextureCoords[0][i] : aiVector3D(0,0,0);

        // compute handedness: sign(dot(cross(n, t), b))
        aiVector3D c = n ^ t;
        float handed = (c * b) < 0.0f ? -1.0f : 1.0f;

        v.pos[0] = p.x; v.pos[1] = p.y; v.pos[2] = p.z;
        v.nrm[0] = n.x; v.nrm[1] = n.y; v.nrm[2] = n.z;
        v.tan[0] = t.x; v.tan[1] = t.y; v.tan[2] = t.z; v.tan[3] = handed;
        v.uv0[0] = uv.x; v.uv0[1] = uv.y;
    }

    m_indices.clear();
    for (unsigned f = 0; f < m->mNumFaces; ++f)
    {
        const aiFace& face = m->mFaces[f];
        if (face.mNumIndices != 3) continue;
        m_indices.push_back(face.mIndices[0]);
        m_indices.push_back(face.mIndices[1]);
        m_indices.push_back(face.mIndices[2]);
    }

    m_indexCount = static_cast<UINT>(m_indices.size());
    return true;
}

void Mesh::CreateBuffers(ID3D12Device* dev, ID3D12GraphicsCommandList* /*cmd*/)
{
    const UINT vbSize = static_cast<UINT>(m_vertices.size() * sizeof(Vertex));
    const UINT ibSize = static_cast<UINT>(m_indices.size() * sizeof(uint32_t));

    m_vb = CreateUpload(dev, vbSize);
    m_ib = CreateUpload(dev, ibSize);

    void* p = nullptr;
    m_vb->Map(0, nullptr, &p); std::memcpy(p, m_vertices.data(), vbSize); m_vb->Unmap(0, nullptr);
    m_ib->Map(0, nullptr, &p); std::memcpy(p, m_indices.data(), ibSize); m_ib->Unmap(0, nullptr);

    m_vbv.BufferLocation = m_vb->GetGPUVirtualAddress();
    m_vbv.SizeInBytes = vbSize;
    m_vbv.StrideInBytes = sizeof(Vertex);

    m_ibv.BufferLocation = m_ib->GetGPUVirtualAddress();
    m_ibv.SizeInBytes = ibSize;
    m_ibv.Format = DXGI_FORMAT_R32_UINT;
}

} // namespace assets
