#pragma once

#include <windows.h>

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <filesystem>

#include "Mesh.h"
#include "Shader.h"

class D3DApp {
public:
    D3DApp()              = default;
    D3DApp(const D3DApp&) = delete;
    D3DApp& operator=(const D3DApp&) = delete;
    ~D3DApp();

    [[nodiscard]] bool Init(HWND hwnd, int width, int height);
    void               OnResize(int width, int height);

    void Update(float dt);
    void Render();

private:
    [[nodiscard]] bool CreateRenderTarget();
    void               ReleaseRenderTarget();
    [[nodiscard]] bool InitPipeline(const std::filesystem::path& shaderDir);
    [[nodiscard]] bool CreateCheckerboardTexture();

    // --- D3D11 core ---
    Microsoft::WRL::ComPtr<ID3D11Device>           mDevice;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext>    mContext;
    Microsoft::WRL::ComPtr<IDXGISwapChain>         mSwapChain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> mRTV;
    D3D11_VIEWPORT                                 mViewport = {};
    int                                            mWidth    = 0;
    int                                            mHeight   = 0;

    // --- Phase 1-2/1-3: shaders, input layout, quad mesh ---
    VertexShader                              mVS;
    PixelShader                               mPS;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> mInputLayout;
    Mesh                                      mMesh;

    // --- Phase 1-4: constant buffer (MVP matrix + tint color) ---
    Microsoft::WRL::ComPtr<ID3D11Buffer>      mPerObjectCB;
    float                                     mAngle = 0.f; // rotation angle (radians)

    // --- Phase 1-5: texture + sampler ---
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> mTextureSRV;
    Microsoft::WRL::ComPtr<ID3D11SamplerState>       mSampler;

    // --- Phase 1-6: per-frame constant buffer (time / deltaTime) ---
    Microsoft::WRL::ComPtr<ID3D11Buffer> mPerFrameCB;
    float                                mTime = 0.f; // accumulated time (seconds)
};
