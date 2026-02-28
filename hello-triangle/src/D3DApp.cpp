#include "D3DApp.h"

#include <iterator>

D3DApp::~D3DApp() {
    // Ensure GPU is done before releasing resources.
    // ComPtr members release automatically in reverse declaration order.
    if (mContext) {
        mContext->ClearState();
        mContext->Flush();
    }
}

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

    return CreateRenderTarget();
}

bool D3DApp::CreateRenderTarget() {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = mSwapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()));
    if (FAILED(hr)) return false;

    hr = mDevice->CreateRenderTargetView(backBuffer.Get(), nullptr, mRTV.GetAddressOf());
    if (FAILED(hr)) return false;

    // Update viewport to match current back buffer dimensions.
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
        0,                          // keep existing buffer count
        static_cast<UINT>(width),
        static_cast<UINT>(height),
        DXGI_FORMAT_UNKNOWN,        // keep existing format
        0
    );
    if (FAILED(hr)) return;

    mWidth  = width;
    mHeight = height;

    if (!CreateRenderTarget()) {
        // Restore old dimensions so the same size can be retried later.
        mWidth  = prevWidth;
        mHeight = prevHeight;
    }
}

void D3DApp::Update([[maybe_unused]] float dt) {
    // Phase 1-1: nothing to update yet.
}

void D3DApp::Render() {
    if (!mContext || !mRTV) return; // guard against failed resize

    // Clear with cornflower blue (classic D3D sample color).
    constexpr float kClearColor[4] = { 0.392f, 0.584f, 0.929f, 1.0f };

    mContext->OMSetRenderTargets(1, mRTV.GetAddressOf(), nullptr);
    mContext->RSSetViewports(1, &mViewport);
    mContext->ClearRenderTargetView(mRTV.Get(), kClearColor);

    mSwapChain->Present(1, 0); // vsync
}
