#include "Shader.h"

#include <fstream>

namespace {

std::vector<std::byte> ReadBinaryFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {};

    const auto size = static_cast<std::size_t>(file.tellg());
    file.seekg(0);

    std::vector<std::byte> buf(size);
    file.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(size));
    return buf;
}

} // namespace

bool VertexShader::Load(ID3D11Device* device, const std::filesystem::path& csoPath) {
    mBytecode = ReadBinaryFile(csoPath);
    if (mBytecode.empty()) return false;

    return SUCCEEDED(device->CreateVertexShader(
        mBytecode.data(),
        mBytecode.size(),
        nullptr,
        mShader.GetAddressOf()
    ));
}

bool PixelShader::Load(ID3D11Device* device, const std::filesystem::path& csoPath) {
    mBytecode = ReadBinaryFile(csoPath);
    if (mBytecode.empty()) return false;

    return SUCCEEDED(device->CreatePixelShader(
        mBytecode.data(),
        mBytecode.size(),
        nullptr,
        mShader.GetAddressOf()
    ));
}
