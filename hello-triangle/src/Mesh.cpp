#include "Mesh.h"

bool Mesh::Create(ID3D11Device* device, std::span<const Vertex> vertices) {
    mStride      = sizeof(Vertex);
    mOffset      = 0;
    mVertexCount = static_cast<UINT>(vertices.size());

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth         = static_cast<UINT>(vertices.size_bytes());
    bd.Usage             = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags         = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA sd = {};
    sd.pSysMem                = vertices.data();

    return SUCCEEDED(device->CreateBuffer(&bd, &sd, mVertexBuffer.GetAddressOf()));
}

void Mesh::Bind(ID3D11DeviceContext* context) const {
    context->IASetVertexBuffers(0, 1, mVertexBuffer.GetAddressOf(), &mStride, &mOffset);
}

void Mesh::Draw(ID3D11DeviceContext* context) const {
    context->Draw(mVertexCount, 0);
}
