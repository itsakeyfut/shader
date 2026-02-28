#include "D3D12App.h"

#include <d3dcompiler.h>    // D3DReadFileToBlob
#include <DirectXMath.h>

#include <iterator>
#include <vector>

#pragma comment(lib, "d3dcompiler.lib")

namespace {

// ---------------------------------------------------------------------------
// Per-vertex layout (must match VSInput in vertex12.hlsl).
// ---------------------------------------------------------------------------
struct Vertex {
    float pos[3]; // POSITION
    float col[4]; // COLOR
};

// ---------------------------------------------------------------------------
// Constant buffer mirroring cbuffer PerObject : register(b0) in vertex12.hlsl.
// D3D12 requires CBV buffers to be a multiple of 256 bytes.
// ---------------------------------------------------------------------------
struct PerObjectCB {
    DirectX::XMFLOAT4X4 mvpMatrix; // 64 bytes
    DirectX::XMFLOAT4   tintColor; // 16 bytes
    uint8_t              _pad[176]; // 176 bytes padding  ->  total = 256 bytes
};
static_assert(sizeof(PerObjectCB) == 256,
    "PerObjectCB must be exactly 256 bytes (D3D12 CBV alignment)");

// ---------------------------------------------------------------------------
// Transition a resource between two states.
// ---------------------------------------------------------------------------
void Transition(ID3D12GraphicsCommandList* list,
                ID3D12Resource*            res,
                D3D12_RESOURCE_STATES      before,
                D3D12_RESOURCE_STATES      after)
{
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource   = res;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter  = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &barrier);
}

// ---------------------------------------------------------------------------
// Load a compiled shader (.cso) from disk into a blob.
// ---------------------------------------------------------------------------
[[nodiscard]] bool LoadCso(const std::filesystem::path& path,
                            Microsoft::WRL::ComPtr<ID3DBlob>& out)
{
    return SUCCEEDED(D3DReadFileToBlob(path.c_str(), out.GetAddressOf()));
}

} // namespace

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

D3D12App::~D3D12App() {
    WaitForGPU(); // ensure GPU is idle before releasing resources
    if (mFenceEvent) {
        CloseHandle(mFenceEvent);
        mFenceEvent = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

bool D3D12App::Init(HWND hwnd, int width, int height) {
    mWidth  = width;
    mHeight = height;

    if (!CreateDeviceAndQueue())                     return false;
    if (!CreateSwapChain(hwnd))                      return false;
    if (!CreateRtvHeapAndViews())                    return false;
    if (!CreateCommandInfrastructure())              return false;
    if (!CreateGeometryAndConstantBuffer())          return false;
    if (!CreateFence())                              return false;

    // Locate compiled shaders next to the exe.
    wchar_t exePath[MAX_PATH] = {};
    const DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (len == 0 || len == MAX_PATH) return false;
    const auto shaderDir = std::filesystem::path(exePath).parent_path() / L"shaders";

    if (!CreateRootSignatureAndPso(shaderDir))       return false;

    UpdateViewportScissor();
    return true;
}

// ---------------------------------------------------------------------------
// CreateDeviceAndQueue
// ---------------------------------------------------------------------------

bool D3D12App::CreateDeviceAndQueue() {
#if defined(D3D_DEBUG_LAYER)
    {
        Microsoft::WRL::ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(debug.GetAddressOf())))) {
            debug->EnableDebugLayer();
        }
    }
    constexpr UINT factoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#else
    constexpr UINT factoryFlags = 0;
#endif

    if (FAILED(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(mFactory.GetAddressOf()))))
        return false;

    // Try the default (hardware) adapter first.
    HRESULT hr = D3D12CreateDevice(
        nullptr,                  // default adapter
        D3D_FEATURE_LEVEL_11_0,
        IID_PPV_ARGS(mDevice.GetAddressOf())
    );
    if (FAILED(hr)) {
        // Fall back to WARP software renderer.
        Microsoft::WRL::ComPtr<IDXGIAdapter> warp;
        if (FAILED(mFactory->EnumWarpAdapter(IID_PPV_ARGS(warp.GetAddressOf()))))
            return false;
        hr = D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0,
                               IID_PPV_ARGS(mDevice.GetAddressOf()));
        if (FAILED(hr)) return false;
    }

    D3D12_COMMAND_QUEUE_DESC qd = {};
    qd.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;
    qd.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    return SUCCEEDED(mDevice->CreateCommandQueue(&qd,
        IID_PPV_ARGS(mCommandQueue.GetAddressOf())));
}

// ---------------------------------------------------------------------------
// CreateSwapChain
// ---------------------------------------------------------------------------

bool D3D12App::CreateSwapChain(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC1 scd = {};
    scd.BufferCount  = kFrameCount;
    scd.Width        = static_cast<UINT>(mWidth);
    scd.Height       = static_cast<UINT>(mHeight);
    scd.Format       = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage  = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.SwapEffect   = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.SampleDesc.Count = 1;

    Microsoft::WRL::ComPtr<IDXGISwapChain1> sc1;
    if (FAILED(mFactory->CreateSwapChainForHwnd(
            mCommandQueue.Get(), hwnd, &scd, nullptr, nullptr,
            sc1.GetAddressOf())))
        return false;

    // Disable Alt+Enter fullscreen toggle.
    mFactory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    if (FAILED(sc1.As(&mSwapChain))) return false;
    mFrameIndex = mSwapChain->GetCurrentBackBufferIndex();
    return true;
}

// ---------------------------------------------------------------------------
// CreateRtvHeapAndViews
// ---------------------------------------------------------------------------

bool D3D12App::CreateRtvHeapAndViews() {
    D3D12_DESCRIPTOR_HEAP_DESC hd = {};
    hd.NumDescriptors = kFrameCount;
    hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(mDevice->CreateDescriptorHeap(&hd,
            IID_PPV_ARGS(mRtvHeap.GetAddressOf()))))
        return false;

    mRtvDescSize = mDevice->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
        mRtvHeap->GetCPUDescriptorHandleForHeapStart();

    for (UINT i = 0; i < kFrameCount; ++i) {
        if (FAILED(mSwapChain->GetBuffer(i,
                IID_PPV_ARGS(mRenderTargets[i].GetAddressOf()))))
            return false;
        mDevice->CreateRenderTargetView(mRenderTargets[i].Get(), nullptr, rtvHandle);
        rtvHandle.ptr += mRtvDescSize;
    }
    return true;
}

// ---------------------------------------------------------------------------
// CreateCommandInfrastructure
// ---------------------------------------------------------------------------

bool D3D12App::CreateCommandInfrastructure() {
    if (FAILED(mDevice->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(mCommandAllocator.GetAddressOf()))))
        return false;

    // Command list is created in closed state; opened in Render().
    if (FAILED(mDevice->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            mCommandAllocator.Get(),
            nullptr,                          // PSO set later in Render()
            IID_PPV_ARGS(mCommandList.GetAddressOf()))))
        return false;

    return SUCCEEDED(mCommandList->Close());
}

// ---------------------------------------------------------------------------
// CreateRootSignatureAndPso
// ---------------------------------------------------------------------------

bool D3D12App::CreateRootSignatureAndPso(const std::filesystem::path& shaderDir) {
    // --- Root signature: one root CBV at VS b0 ---
    D3D12_ROOT_PARAMETER param = {};
    param.ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    param.ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
    param.Descriptor.ShaderRegister = 0; // b0
    param.Descriptor.RegisterSpace  = 0;

    D3D12_ROOT_SIGNATURE_DESC rsd = {};
    rsd.NumParameters     = 1;
    rsd.pParameters       = &param;
    rsd.Flags             =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS       |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS     |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS   |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;

    Microsoft::WRL::ComPtr<ID3DBlob> sig, err;
    if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1,
            sig.GetAddressOf(), err.GetAddressOf())))
        return false;

    if (FAILED(mDevice->CreateRootSignature(
            0, sig->GetBufferPointer(), sig->GetBufferSize(),
            IID_PPV_ARGS(mRootSignature.GetAddressOf()))))
        return false;

    // --- Load compiled shaders ---
    Microsoft::WRL::ComPtr<ID3DBlob> vsBytecode, psBytecode;
    if (!LoadCso(shaderDir / L"vertex12.cso", vsBytecode)) return false;
    if (!LoadCso(shaderDir / L"pixel12.cso",  psBytecode)) return false;

    // --- Input layout: POSITION (float3) + COLOR (float4) ---
    const D3D12_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT,  0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    // --- PSO ---
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psd = {};
    psd.pRootSignature        = mRootSignature.Get();
    psd.VS                    = { vsBytecode->GetBufferPointer(), vsBytecode->GetBufferSize() };
    psd.PS                    = { psBytecode->GetBufferPointer(),  psBytecode->GetBufferSize()  };
    psd.InputLayout           = { layout, static_cast<UINT>(std::size(layout)) };
    psd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psd.NumRenderTargets      = 1;
    psd.RTVFormats[0]         = DXGI_FORMAT_R8G8B8A8_UNORM;
    psd.DSVFormat             = DXGI_FORMAT_UNKNOWN; // no depth buffer

    // Default rasterizer: solid fill, back-face cull (CW = front).
    psd.RasterizerState.FillMode              = D3D12_FILL_MODE_SOLID;
    psd.RasterizerState.CullMode              = D3D12_CULL_MODE_BACK;
    psd.RasterizerState.FrontCounterClockwise = FALSE;
    psd.RasterizerState.DepthClipEnable       = TRUE;

    // Opaque blend state.
    psd.BlendState.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;

    // Depth stencil disabled.
    psd.DepthStencilState.DepthEnable   = FALSE;
    psd.DepthStencilState.StencilEnable = FALSE;

    psd.SampleMask                      = UINT_MAX;
    psd.SampleDesc.Count                = 1;

    return SUCCEEDED(mDevice->CreateGraphicsPipelineState(
        &psd, IID_PPV_ARGS(mPso.GetAddressOf())));
}

// ---------------------------------------------------------------------------
// CreateGeometryAndConstantBuffer
// ---------------------------------------------------------------------------

bool D3D12App::CreateGeometryAndConstantBuffer() {
    // --- Vertex buffer (upload heap — simple, adequate for static geometry) ---
    const Vertex kTriangle[] = {
        { { 0.0f,  0.5f, 0.0f}, {1.f, 0.f, 0.f, 1.f} }, // top   — red
        { { 0.5f, -0.5f, 0.0f}, {0.f, 1.f, 0.f, 1.f} }, // right — green
        { {-0.5f, -0.5f, 0.0f}, {0.f, 0.f, 1.f, 1.f} }, // left  — blue
    };
    const UINT vbSize = sizeof(kTriangle);

    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width              = vbSize;
    rd.Height             = 1;
    rd.DepthOrArraySize   = 1;
    rd.MipLevels          = 1;
    rd.Format             = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc.Count   = 1;
    rd.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    if (FAILED(mDevice->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(mVertexBuffer.GetAddressOf()))))
        return false;

    void* vbData = nullptr;
    const D3D12_RANGE readRange = { 0, 0 }; // CPU will not read back
    if (FAILED(mVertexBuffer->Map(0, &readRange, &vbData))) return false;
    memcpy(vbData, kTriangle, vbSize);
    mVertexBuffer->Unmap(0, nullptr);

    mVBView.BufferLocation = mVertexBuffer->GetGPUVirtualAddress();
    mVBView.StrideInBytes  = sizeof(Vertex);
    mVBView.SizeInBytes    = vbSize;

    // --- Constant buffer (upload heap, 256-byte aligned, persistently mapped) ---
    rd.Width = sizeof(PerObjectCB); // 256

    if (FAILED(mDevice->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(mConstantBuffer.GetAddressOf()))))
        return false;

    // Persistently map; never unmap (valid until the resource is destroyed).
    return SUCCEEDED(mConstantBuffer->Map(0, &readRange, &mCbMapped));
}

// ---------------------------------------------------------------------------
// CreateFence
// ---------------------------------------------------------------------------

bool D3D12App::CreateFence() {
    mFenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!mFenceEvent) return false;

    return SUCCEEDED(mDevice->CreateFence(
        mFenceValue, D3D12_FENCE_FLAG_NONE,
        IID_PPV_ARGS(mFence.GetAddressOf())));
}

// ---------------------------------------------------------------------------
// WaitForGPU — signal the fence then block until the GPU has processed it.
// ---------------------------------------------------------------------------

void D3D12App::WaitForGPU() {
    if (!mCommandQueue || !mFence || !mFenceEvent) return;

    ++mFenceValue;
    mCommandQueue->Signal(mFence.Get(), mFenceValue);
    if (mFence->GetCompletedValue() < mFenceValue) {
        mFence->SetEventOnCompletion(mFenceValue, mFenceEvent);
        WaitForSingleObject(mFenceEvent, INFINITE);
    }
}

// ---------------------------------------------------------------------------
// UpdateViewportScissor
// ---------------------------------------------------------------------------

void D3D12App::UpdateViewportScissor() {
    mViewport.TopLeftX = 0.f;
    mViewport.TopLeftY = 0.f;
    mViewport.Width    = static_cast<float>(mWidth);
    mViewport.Height   = static_cast<float>(mHeight);
    mViewport.MinDepth = 0.f;
    mViewport.MaxDepth = 1.f;

    mScissor = { 0, 0, mWidth, mHeight };
}

// ---------------------------------------------------------------------------
// OnResize
// ---------------------------------------------------------------------------

void D3D12App::OnResize(int width, int height) {
    if (width == 0 || height == 0) return;
    if (width == mWidth && height == mHeight) return;

    WaitForGPU();

    // Release RTV references held by this class.
    for (UINT i = 0; i < kFrameCount; ++i)
        mRenderTargets[i].Reset();

    HRESULT hr = mSwapChain->ResizeBuffers(
        kFrameCount,
        static_cast<UINT>(width), static_cast<UINT>(height),
        DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    if (FAILED(hr)) return;

    mWidth      = width;
    mHeight     = height;
    mFrameIndex = mSwapChain->GetCurrentBackBufferIndex();

    // Recreate RTVs.
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
        mRtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < kFrameCount; ++i) {
        mSwapChain->GetBuffer(i, IID_PPV_ARGS(mRenderTargets[i].GetAddressOf()));
        mDevice->CreateRenderTargetView(mRenderTargets[i].Get(), nullptr, rtvHandle);
        rtvHandle.ptr += mRtvDescSize;
    }

    UpdateViewportScissor();
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------

void D3D12App::Update(float dt) {
    mAngle += dt;
    if (mAngle > DirectX::XM_2PI) mAngle -= DirectX::XM_2PI;

    if (!mCbMapped) return;

    // Build MVP matrix (same camera/projection as D3D11 version).
    const DirectX::XMMATRIX model = DirectX::XMMatrixRotationY(mAngle);

    const DirectX::XMVECTOR eye    = DirectX::XMVectorSet(0.f, 0.f, -2.f, 0.f);
    const DirectX::XMVECTOR target = DirectX::XMVectorZero();
    const DirectX::XMVECTOR up     = DirectX::XMVectorSet(0.f, 1.f,  0.f, 0.f);
    const DirectX::XMMATRIX view   = DirectX::XMMatrixLookAtLH(eye, target, up);

    const float aspect = (mHeight > 0)
        ? static_cast<float>(mWidth) / static_cast<float>(mHeight) : 1.f;
    const DirectX::XMMATRIX proj =
        DirectX::XMMatrixPerspectiveFovLH(DirectX::XM_PIDIV4, aspect, 0.1f, 100.f);

    // Transpose: row-major (DirectXMath) -> column-major (HLSL).
    const DirectX::XMMATRIX mvp = DirectX::XMMatrixTranspose(model * view * proj);

    auto* cb = static_cast<PerObjectCB*>(mCbMapped);
    DirectX::XMStoreFloat4x4(&cb->mvpMatrix, mvp);
    cb->tintColor = { 1.f, 1.f, 1.f, 1.f }; // no tint
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

void D3D12App::Render() {
    if (!mCommandList || !mRenderTargets[mFrameIndex]) return;

    // --- Reset command allocator and list ---
    mCommandAllocator->Reset();
    mCommandList->Reset(mCommandAllocator.Get(), mPso.Get());

    // --- Set global state ---
    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());
    mCommandList->RSSetViewports(1, &mViewport);
    mCommandList->RSSetScissorRects(1, &mScissor);

    // --- Transition back buffer: PRESENT -> RENDER_TARGET ---
    Transition(mCommandList.Get(),
               mRenderTargets[mFrameIndex].Get(),
               D3D12_RESOURCE_STATE_PRESENT,
               D3D12_RESOURCE_STATE_RENDER_TARGET);

    // --- Set and clear RTV ---
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
        mRtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtvHandle.ptr += static_cast<SIZE_T>(mFrameIndex) * mRtvDescSize;

    constexpr float kClearColor[4] = { 0.392f, 0.584f, 0.929f, 1.0f };
    mCommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
    mCommandList->ClearRenderTargetView(rtvHandle, kClearColor, 0, nullptr);

    // --- Draw triangle ---
    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    mCommandList->IASetVertexBuffers(0, 1, &mVBView);
    mCommandList->SetGraphicsRootConstantBufferView(
        0, mConstantBuffer->GetGPUVirtualAddress());
    mCommandList->DrawInstanced(3, 1, 0, 0);

    // --- Transition back buffer: RENDER_TARGET -> PRESENT ---
    Transition(mCommandList.Get(),
               mRenderTargets[mFrameIndex].Get(),
               D3D12_RESOURCE_STATE_RENDER_TARGET,
               D3D12_RESOURCE_STATE_PRESENT);

    // --- Submit ---
    mCommandList->Close();
    ID3D12CommandList* lists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(1, lists);

    // --- Present (vsync) ---
    mSwapChain->Present(1, 0);

    // --- Advance frame index and wait (simple full-sync pattern) ---
    mFrameIndex = mSwapChain->GetCurrentBackBufferIndex();
    WaitForGPU();
}
