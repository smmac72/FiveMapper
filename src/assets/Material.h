#pragma once
#include <wrl/client.h>
#include <d3d12.h>
#include <array>
#include <string>

namespace assets
{

// fixed texture slots for g-buffer pass
// t0: albedo (srgb), t1: normal (linear), t2: orm (linear: r=metallic, g=ao, b=unused), t3: emissive (linear)
// t4..t6 are reserved for future use
class Material
{
public:
    enum Slot : int
    {
        Albedo   = 0,
        Normal   = 1,
        ORM      = 2,
        Emissive = 3,
        _Count   = 7
    };

public:
    Material() = default;

    // create a shader-visible heap with 7 descriptors and fill with 1x1 fallbacks
    // cmd_list must be open; this function records upload + barriers for fallbacks
    void Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmd_list);

    // load a file into given slot; replaces fallback srvs for that slot
    // is_srgb must be true only for albedo; others are linear
    void LoadSlot(ID3D12Device* device, ID3D12GraphicsCommandList* cmd_list, Slot slot, const std::wstring& path, bool is_srgb);

    // heap and table start for binding (use SetDescriptorHeaps and SetGraphicsRootDescriptorTable)
    ID3D12DescriptorHeap* GetSrvHeap() const { return m_srvHeap.Get(); }
    D3D12_GPU_DESCRIPTOR_HANDLE GetTableStart() const { return m_srvGpuStart; }

private:
    // write srv for a given resource into heap slot
    void WriteSRV(ID3D12Device* device, int slot, ID3D12Resource* res, bool is_srgb);

private:
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvHeap;
    UINT m_stride = 0;
    D3D12_GPU_DESCRIPTOR_HANDLE m_srvGpuStart{};

    // keep resources alive
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, _Count> m_textures{};
};

} // namespace assets
