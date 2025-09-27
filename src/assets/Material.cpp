#include "Material.h"
#include "TextureLoader.h"
#include <stdexcept>
#include <string>

using Microsoft::WRL::ComPtr;

namespace assets
{

void Material::Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmd_list)
{
    // create shader-visible heap for 7 srvs
    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = _Count;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    if (FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_srvHeap))))
        throw std::runtime_error("material: srv heap creation failed");

    m_stride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_srvGpuStart = m_srvHeap->GetGPUDescriptorHandleForHeapStart();

    // t0 albedo (srgb): warm orange (linear)
    {
        m_textures[Albedo] = TextureLoader::CreateSolid1x1(device, cmd_list, 0.10f, 0.45f, 0.30f, 1.0f, /*srgb*/true);
        WriteSRV(device, Albedo, m_textures[Albedo].Get(), /*is_srgb*/true);
    }
    // t1 normal (linear): flat normal (0.5,0.5,1.0)
    {
        m_textures[Normal] = TextureLoader::CreateSolid1x1(device, cmd_list, 0.5f, 0.5f, 1.0f, 1.0f, /*srgb*/false);
        WriteSRV(device, Normal, m_textures[Normal].Get(), /*is_srgb*/false);
    }
    // t2 orm (linear) expected packing: r=ao, g=roughness, b=metallic
    {
        m_textures[ORM] = TextureLoader::CreateSolid1x1(device, cmd_list, 1.0f, 0.6f, 0.1f, 1.0f, /*srgb*/false);
        WriteSRV(device, ORM, m_textures[ORM].Get(), /*is_srgb*/false);
    }
    // t3 emissive (linear): black
    {
        m_textures[Emissive] = TextureLoader::CreateSolid1x1(device, cmd_list, 0.0f, 0.0f, 0.0f, 1.0f, /*srgb*/false);
        WriteSRV(device, Emissive, m_textures[Emissive].Get(), /*is_srgb*/false);
    }
    // t4..t6 reserved: black
    for (int i = 4; i < _Count; ++i)
    {
        m_textures[i] = TextureLoader::CreateSolid1x1(device, cmd_list, 0.0f, 0.0f, 0.0f, 1.0f, /*srgb*/false);
        WriteSRV(device, i, m_textures[i].Get(), /*is_srgb*/false);
    }
}

void Material::LoadSlot(ID3D12Device* device, ID3D12GraphicsCommandList* cmd_list,
                        Slot slot, const std::wstring& path, bool is_srgb)
{
    // safety: keep current texture if anything goes wrong
    if (slot < 0 || slot >= _Count) return;

    // cmd list must be in recording state (reset + not closed)
    // we cannot query this reliably, so we try/catch and fall back on failure
    try
    {
        ComPtr<ID3D12Resource> tex = TextureLoader::LoadFromFile(device, cmd_list, path, is_srgb);
        if (!tex)
        {
            // keep previous fallback, do not throw
            OutputDebugStringW((L"[material] failed to load texture (null): " + path + L"\n").c_str());
            return;
        }

        m_textures[slot] = tex;
        WriteSRV(device, static_cast<int>(slot), m_textures[slot].Get(), is_srgb);
    }
    catch (const std::exception& e)
    {
        // keep previous fallback; log and continue
        std::wstring msg = L"[material] load slot failed (std::exception): ";
        msg += path;
        msg += L"\n";
        OutputDebugStringW(msg.c_str());
        OutputDebugStringA(e.what());
        OutputDebugStringA("\n");
    }
    catch (...)
    {
        std::wstring msg = L"[material] load slot failed (unknown): ";
        msg += path;
        msg += L"\n";
        OutputDebugStringW(msg.c_str());
    }
}

void Material::WriteSRV(ID3D12Device* device, int slot, ID3D12Resource* res, bool is_srgb)
{
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    cpu.ptr += static_cast<SIZE_T>(slot) * m_stride;
    TextureLoader::CreateSRV(device, res, cpu, is_srgb);
}

} // namespace assets
