#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <span>

// Per-vertex data layout (must match D3D11_INPUT_ELEMENT_DESC in D3DApp).
struct Vertex {
    float pos[3]; // xyz  (NDC in Phase 1; world-space from Phase 1-4+)
    float col[4]; // rgba
    float uv[2];  // texture coordinates (u=left→right, v=top→bottom in D3D)
};

// Holds an immutable vertex buffer and issues draw calls.
class Mesh {
public:
    [[nodiscard]] bool Create(ID3D11Device* device, std::span<const Vertex> vertices);

    // Bind vertex buffer to IA stage (slot 0).
    void Bind(ID3D11DeviceContext* context) const;

    // Draw all vertices as a non-indexed draw call.
    void Draw(ID3D11DeviceContext* context) const;

private:
    Microsoft::WRL::ComPtr<ID3D11Buffer> mVertexBuffer;
    UINT                                 mStride      = 0;
    UINT                                 mOffset      = 0;
    UINT                                 mVertexCount = 0;
};
