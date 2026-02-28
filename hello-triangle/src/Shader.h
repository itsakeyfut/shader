#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <cstddef>
#include <filesystem>
#include <vector>

// Loads a compiled shader object (.cso) and creates a vertex shader.
// Retains the bytecode so D3DApp can use it for CreateInputLayout.
class VertexShader {
public:
    [[nodiscard]] bool  Load(ID3D11Device* device, const std::filesystem::path& csoPath);

    ID3D11VertexShader* Get()          const { return mShader.Get(); }
    const void*         Bytecode()     const { return mBytecode.data(); }
    std::size_t         BytecodeSize() const { return mBytecode.size(); }

private:
    Microsoft::WRL::ComPtr<ID3D11VertexShader> mShader;
    std::vector<std::byte>                     mBytecode;
};

// Loads a compiled shader object (.cso) and creates a pixel shader.
class PixelShader {
public:
    [[nodiscard]] bool Load(ID3D11Device* device, const std::filesystem::path& csoPath);

    ID3D11PixelShader* Get() const { return mShader.Get(); }

private:
    Microsoft::WRL::ComPtr<ID3D11PixelShader> mShader;
    std::vector<std::byte>                    mBytecode;
};
