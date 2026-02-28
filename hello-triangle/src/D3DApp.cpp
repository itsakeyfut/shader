#include "D3DApp.h"

#include <DirectXMath.h>
#include <iterator>

namespace {

// Mirrors cbuffer PerObject : register(b0) in vertex.hlsl.
// Size: 64 + 16 = 80 bytes  (multiple of 16 — D3D11 requirement).
struct alignas(16) PerObjectCB {
    DirectX::XMFLOAT4X4 mvpMatrix; // 64 bytes
    DirectX::XMFLOAT4   tintColor; // 16 bytes
};
static_assert(sizeof(PerObjectCB) % 16 == 0,
    "PerObjectCB must be a multiple of 16 bytes");

// Pack RGBA into a uint32_t whose byte layout matches DXGI_FORMAT_R8G8B8A8_UNORM.
// On little-endian systems the bytes land as [R][G][B][A] in memory.
constexpr uint32_t PackRGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 0xFF) {
    return static_cast<uint32_t>(r)
         | (static_cast<uint32_t>(g) <<  8)
         | (static_cast<uint32_t>(b) << 16)
         | (static_cast<uint32_t>(a) << 24);
}

} // namespace

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

D3DApp::~D3DApp() {
    // Ensure GPU is done before releasing resources.
    // ComPtr members release automatically in reverse declaration order.
    if (mContext) {
        mContext->ClearState();
        mContext->Flush();
    }
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

bool D3DApp::Init(HWND hwnd, int width, int height) {
    mWidth  = width;
    mHeight = height;

    // --- Swap chain description ---
    DXGI_SWAP_CHAIN_DESC scd             = {};
    scd.BufferCount                      = 2;
    scd.BufferDesc.Width                 = static_cast<UINT>(width);
    scd.BufferDesc.Height                = static_cast<UINT>(height);
    scd.BufferDesc.Format                = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator = 60;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferUsage                      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow                     = hwnd;
    scd.SampleDesc.Count                 = 1;
    scd.SampleDesc.Quality               = 0;
    scd.Windowed                         = TRUE;
    scd.SwapEffect                       = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.Flags                            = 0;

    // --- Device feature levels ---
    // D3D_FEATURE_LEVEL_11_1 causes E_INVALIDARG on systems without the 11.1
    // runtime; fall back to 11_0-only in that case.
    constexpr D3D_FEATURE_LEVEL kFeatureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };
    constexpr D3D_FEATURE_LEVEL kFallbackFeatureLevels[] = {
        D3D_FEATURE_LEVEL_11_0,
    };

    UINT createFlags = 0;
#if defined(D3D_DEBUG_LAYER)
    createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    // Helper lambda to avoid repeating the long argument list.
    auto tryCreate = [&](UINT flags, const D3D_FEATURE_LEVEL* levels, UINT count) {
        return D3D11CreateDeviceAndSwapChain(
            nullptr,                  // default adapter
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,                  // no software rasterizer
            flags,
            levels,
            count,
            D3D11_SDK_VERSION,
            &scd,
            mSwapChain.GetAddressOf(),
            mDevice.GetAddressOf(),
            nullptr,                  // don't care about achieved feature level
            mContext.GetAddressOf()
        );
    };

    HRESULT hr = tryCreate(createFlags, kFeatureLevels, static_cast<UINT>(std::size(kFeatureLevels)));
    if (hr == E_INVALIDARG) {
        // 11.1 runtime not present; retry with 11.0 only.
        hr = tryCreate(createFlags, kFallbackFeatureLevels, static_cast<UINT>(std::size(kFallbackFeatureLevels)));
    }

    if (FAILED(hr)) {
        // Retry without debug layer in case the D3D11 debug runtime is not installed.
        createFlags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = tryCreate(createFlags, kFeatureLevels, static_cast<UINT>(std::size(kFeatureLevels)));
        if (hr == E_INVALIDARG) {
            hr = tryCreate(createFlags, kFallbackFeatureLevels, static_cast<UINT>(std::size(kFallbackFeatureLevels)));
        }
    }

    if (FAILED(hr)) {
        return false;
    }

    if (!CreateRenderTarget()) {
        return false;
    }

    // Locate compiled shaders in a "shaders/" subdirectory next to the exe.
    wchar_t exePath[MAX_PATH] = {};
    const DWORD pathLen = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (pathLen == 0 || pathLen == MAX_PATH) return false; // failed or truncated
    const auto shaderDir = std::filesystem::path(exePath).parent_path() / L"shaders";

    return InitPipeline(shaderDir);
}

// ---------------------------------------------------------------------------
// InitPipeline
// ---------------------------------------------------------------------------

bool D3DApp::InitPipeline(const std::filesystem::path& shaderDir) {
    // Load vertex and pixel shaders from pre-compiled .cso files.
    if (!mVS.Load(mDevice.Get(), shaderDir / L"vertex.cso")) return false;
    if (!mPS.Load(mDevice.Get(), shaderDir / L"pixel.cso"))  return false;

    // Input layout — must match the Vertex struct and VSInput in vertex.hlsl.
    // Offsets: pos=0 (12 B), col=12 (16 B), uv=28 (8 B). Stride = 36 B.
    const D3D11_INPUT_ELEMENT_DESC layoutDesc[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,   0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    HRESULT hr = mDevice->CreateInputLayout(
        layoutDesc,
        static_cast<UINT>(std::size(layoutDesc)),
        mVS.Bytecode(),
        mVS.BytecodeSize(),
        mInputLayout.GetAddressOf()
    );
    if (FAILED(hr)) return false;

    // Dynamic constant buffer for per-object data updated every frame.
    D3D11_BUFFER_DESC cbd = {};
    cbd.ByteWidth      = sizeof(PerObjectCB);
    cbd.Usage          = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(mDevice->CreateBuffer(&cbd, nullptr, mPerObjectCB.GetAddressOf()))) {
        return false;
    }

    // Procedural 64x64 checkerboard texture (white / cornflower-blue cells).
    if (!CreateCheckerboardTexture()) return false;

    // Linear-wrap sampler.
    D3D11_SAMPLER_DESC sd = {};
    sd.Filter   = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.MaxLOD   = D3D11_FLOAT32_MAX;
    if (FAILED(mDevice->CreateSamplerState(&sd, mSampler.GetAddressOf()))) return false;

    // Quad vertices — two CW triangles forming a unit square in the XY plane.
    // D3D UV convention: u = left→right (0→1), v = top→bottom (0→1).
    const Vertex kQuad[] = {
        //  pos                    col           uv
        { {-0.5f,  0.5f, 0.f}, {1,1,1,1}, {0.f, 0.f} }, // top-left
        { { 0.5f,  0.5f, 0.f}, {1,1,1,1}, {1.f, 0.f} }, // top-right
        { {-0.5f, -0.5f, 0.f}, {1,1,1,1}, {0.f, 1.f} }, // bottom-left
        { { 0.5f,  0.5f, 0.f}, {1,1,1,1}, {1.f, 0.f} }, // top-right    (tri 2)
        { { 0.5f, -0.5f, 0.f}, {1,1,1,1}, {1.f, 1.f} }, // bottom-right
        { {-0.5f, -0.5f, 0.f}, {1,1,1,1}, {0.f, 1.f} }, // bottom-left
    };
    return mMesh.Create(mDevice.Get(), kQuad);
}

// ---------------------------------------------------------------------------
// CreateCheckerboardTexture
// ---------------------------------------------------------------------------

bool D3DApp::CreateCheckerboardTexture() {
    constexpr int kSize     = 64; // texture dimensions (64x64 texels)
    constexpr int kCellSize =  8; // checkerboard cell size in texels

    uint32_t pixels[kSize * kSize];
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const bool even = ((x / kCellSize) + (y / kCellSize)) % 2 == 0;
            pixels[y * kSize + x] = even
                ? PackRGBA(255, 255, 255)   // white
                : PackRGBA(100, 149, 237);  // cornflower blue
        }
    }

    D3D11_TEXTURE2D_DESC td = {};
    td.Width            = kSize;
    td.Height           = kSize;
    td.MipLevels        = 1;
    td.ArraySize        = 1;
    td.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage            = D3D11_USAGE_IMMUTABLE;
    td.BindFlags        = D3D11_BIND_SHADER_RESOURCE;

    const D3D11_SUBRESOURCE_DATA initData { pixels, kSize * sizeof(uint32_t), 0 };

    Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
    if (FAILED(mDevice->CreateTexture2D(&td, &initData, tex.GetAddressOf()))) return false;

    return SUCCEEDED(mDevice->CreateShaderResourceView(
        tex.Get(), nullptr, mTextureSRV.GetAddressOf()));
}

// ---------------------------------------------------------------------------
// RTV helpers
// ---------------------------------------------------------------------------

bool D3DApp::CreateRenderTarget() {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = mSwapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()));
    if (FAILED(hr)) return false;

    hr = mDevice->CreateRenderTargetView(backBuffer.Get(), nullptr, mRTV.GetAddressOf());
    if (FAILED(hr)) return false;

    mViewport.TopLeftX = 0.0f;
    mViewport.TopLeftY = 0.0f;
    mViewport.Width    = static_cast<float>(mWidth);
    mViewport.Height   = static_cast<float>(mHeight);
    mViewport.MinDepth = 0.0f;
    mViewport.MaxDepth = 1.0f;

    return true;
}

void D3DApp::ReleaseRenderTarget() {
    mContext->OMSetRenderTargets(0, nullptr, nullptr);
    mRTV.Reset();
}

void D3DApp::OnResize(int width, int height) {
    if (width == 0 || height == 0) return;
    if (width == mWidth && height == mHeight) return;

    const int prevWidth  = mWidth;
    const int prevHeight = mHeight;

    ReleaseRenderTarget();

    HRESULT hr = mSwapChain->ResizeBuffers(
        0,
        static_cast<UINT>(width),
        static_cast<UINT>(height),
        DXGI_FORMAT_UNKNOWN,
        0
    );
    if (FAILED(hr)) return;

    mWidth  = width;
    mHeight = height;

    if (!CreateRenderTarget()) {
        mWidth  = prevWidth;
        mHeight = prevHeight;
    }
}

// ---------------------------------------------------------------------------
// Per-frame
// ---------------------------------------------------------------------------

void D3DApp::Update(float dt) {
    // Rotate at 1 radian per second; wrap to avoid float drift over time.
    mAngle += dt;
    if (mAngle > DirectX::XM_2PI) mAngle -= DirectX::XM_2PI;

    if (!mPerObjectCB) return;

    // --- Build MVP matrix ---
    const DirectX::XMMATRIX model = DirectX::XMMatrixRotationY(mAngle);

    const DirectX::XMVECTOR eye    = DirectX::XMVectorSet(0.f, 0.f, -2.f, 0.f);
    const DirectX::XMVECTOR target = DirectX::XMVectorZero();
    const DirectX::XMVECTOR up     = DirectX::XMVectorSet(0.f, 1.f,  0.f, 0.f);
    const DirectX::XMMATRIX view   = DirectX::XMMatrixLookAtLH(eye, target, up);

    const float aspect = (mHeight > 0)
        ? static_cast<float>(mWidth) / static_cast<float>(mHeight)
        : 1.f;
    const DirectX::XMMATRIX proj = DirectX::XMMatrixPerspectiveFovLH(
        DirectX::XM_PIDIV4, aspect, 0.1f, 100.f);

    // Transpose: DirectXMath stores row-major; HLSL float4x4 is column-major.
    const DirectX::XMMATRIX mvp = DirectX::XMMatrixTranspose(model * view * proj);

    // --- Upload to GPU via Map / Unmap ---
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(mContext->Map(mPerObjectCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        auto* cb = static_cast<PerObjectCB*>(mapped.pData);
        DirectX::XMStoreFloat4x4(&cb->mvpMatrix, mvp);
        cb->tintColor = { 1.f, 1.f, 1.f, 1.f }; // no tint
        mContext->Unmap(mPerObjectCB.Get(), 0);
    }
}

void D3DApp::Render() {
    if (!mContext || !mRTV) return; // guard against failed resize

    // --- Clear ---
    constexpr float kClearColor[4] = { 0.392f, 0.584f, 0.929f, 1.0f };
    mContext->OMSetRenderTargets(1, mRTV.GetAddressOf(), nullptr);
    mContext->RSSetViewports(1, &mViewport);
    mContext->ClearRenderTargetView(mRTV.Get(), kClearColor);

    // --- Bind pipeline state ---
    mContext->VSSetShader(mVS.Get(), nullptr, 0);
    mContext->PSSetShader(mPS.Get(), nullptr, 0);
    mContext->IASetInputLayout(mInputLayout.Get());
    mContext->VSSetConstantBuffers(0, 1, mPerObjectCB.GetAddressOf());
    mContext->PSSetShaderResources(0, 1, mTextureSRV.GetAddressOf());
    mContext->PSSetSamplers(0, 1, mSampler.GetAddressOf());

    // --- Draw quad ---
    mMesh.Bind(mContext.Get());
    mContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    mMesh.Draw(mContext.Get());

    mSwapChain->Present(1, 0); // vsync
}
