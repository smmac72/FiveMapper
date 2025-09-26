#include "ShaderUtils.h"
#include <d3dcompiler.h>
#include <fstream>
#include <stdexcept>

using Microsoft::WRL::ComPtr;

namespace dx12
{

Microsoft::WRL::ComPtr<ID3DBlob> ReadFileToBlob(const std::wstring& path)
{
    // open file at end to get size
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
    {
        throw std::runtime_error("shader file open failed");
    }

    std::streamsize size = f.tellg();
    f.seekg(0, std::ios::beg);

    ComPtr<ID3DBlob> blob;
    if (FAILED(D3DCreateBlob(static_cast<SIZE_T>(size), &blob)))
    {
        throw std::runtime_error("D3DCreateBlob failed");
    }

    if (!f.read(static_cast<char*>(blob->GetBufferPointer()), size))
    {
        throw std::runtime_error("shader file read failed");
    }

    return blob;
}

}