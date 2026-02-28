#pragma once

#include <windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <filesystem>

// ---------------------------------------------------------------------------
// D3D12App — D3D12 port of Phase 1 hello-triangle (rotating RGB triangle).
//
// Key D3D12 concepts demonstrated:
//   • ID3D12Device + ID3D12CommandQueue
//   • IDXGISwapChain3 with double-buffered RTVs
//   • ID3D12DescriptorHeap (RTV)
//   • ID3D12RootSignature with a single CBV root descriptor (b0)
//   • ID3D12PipelineState (PSO)
//   • Resource barriers: PRESENT <-> RENDER_TARGET
//   • Constant buffer on upload heap (persistently mapped, 256-byte aligned)
//   • Fence-based CPU/GPU synchronization
// ---------------------------------------------------------------------------
class D3D12App {
public:
    D3D12App()              = default;
    D3D12App(const D3D12App&) = delete;
    D3D12App& operator=(const D3D12App&) = delete;
    ~D3D12App();

    [[nodiscard]] bool Init(HWND hwnd, int width, int height);
    void               OnResize(int width, int height);

    void Update(float dt);
    void Render();

private:
    static constexpr UINT kFrameCount = 2; // double-buffered swap chain

    // --- Init helpers ---
    [[nodiscard]] bool CreateDeviceAndQueue();
    [[nodiscard]] bool CreateSwapChain(HWND hwnd);
    [[nodiscard]] bool CreateRtvHeapAndViews();
    [[nodiscard]] bool CreateCommandInfrastructure();
    [[nodiscard]] bool CreateRootSignatureAndPso(const std::filesystem::path& shaderDir);
    [[nodiscard]] bool CreateGeometryAndConstantBuffer();
    [[nodiscard]] bool CreateFence();

    // --- Per-frame helpers ---
    void RecordCommands();
    void WaitForGPU();
    void UpdateViewportScissor();

    // DXGI factory (kept alive for resize)
    Microsoft::WRL::ComPtr<IDXGIFactory4> mFactory;

    // --- D3D12 core ---
    Microsoft::WRL::ComPtr<ID3D12Device>       mDevice;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> mCommandQueue;
    Microsoft::WRL::ComPtr<IDXGISwapChain3>    mSwapChain;
    UINT                                        mFrameIndex = 0;

    // --- RTV descriptor heap ---
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mRtvHeap;
    UINT                                          mRtvDescSize = 0;

    // --- Render targets (one per swap-chain buffer) ---
    Microsoft::WRL::ComPtr<ID3D12Resource> mRenderTargets[kFrameCount];

    // --- Command infrastructure (single allocator; full sync each frame) ---
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator>    mCommandAllocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> mCommandList;

    // --- Pipeline state ---
    Microsoft::WRL::ComPtr<ID3D12RootSignature> mRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mPso;

    // --- Vertex buffer (upload heap) ---
    Microsoft::WRL::ComPtr<ID3D12Resource> mVertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW               mVBView = {};

    // --- Constant buffer (upload heap, persistently mapped, 256-byte aligned) ---
    Microsoft::WRL::ComPtr<ID3D12Resource> mConstantBuffer;
    void*                                  mCbMapped = nullptr; // persistent map

    // --- CPU/GPU synchronization ---
    Microsoft::WRL::ComPtr<ID3D12Fence> mFence;
    UINT64                              mFenceValue = 0;
    HANDLE                              mFenceEvent = nullptr;

    // --- Render state ---
    D3D12_VIEWPORT mViewport = {};
    D3D12_RECT     mScissor  = {};
    int            mWidth    = 0;
    int            mHeight   = 0;
    float          mAngle    = 0.f;
};
