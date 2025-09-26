#pragma once
#include <wrl/client.h>
#include <d3dcommon.h>
#include <string>

namespace dx12
{

// read compiled shader (.cso) into a blob
Microsoft::WRL::ComPtr<ID3DBlob> ReadFileToBlob(const std::wstring& path);

}