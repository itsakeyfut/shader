---
description: Start implementing a GitHub issue
allowed-tools: ["bash", "read", "write", "edit", "glob", "grep", "task"]
argument-hint: "<issue-number>"
---

First, fetch the issue details:

```bash
gh issue view $1
```

Now proceed with implementing this issue.

**Development Guidelines:**
- All comments and documentation must be written in English
- HLSL: follow HLSL coding conventions (PascalCase for functions/structs, camelCase for locals)
- C++: follow modern C++17 patterns, RAII for D3D COM objects (ComPtr<T>)
- Keep shader code and host code clearly separated
- Consider performance: minimize CPU-GPU synchronization, batch draw calls
- Use D3D debug layer during development

**Before starting:**
1. Review the issue requirements carefully
2. Identify the target phase and API:
   - Phase 1-2: Direct3D 11 + fxc (Shader Model 5.x)
   - Phase 3-4: D3D11/12 + dxc (Shader Model 6.x)
   - Phase 5+: Direct3D 12 + dxc
   - Phase 7: D3D12 DXR (ray tracing)
   - Phase 13: Slang compiler
3. Identify affected components:
   - Window / message loop (`main.cpp`)
   - D3D device initialization (`D3DApp.cpp/.h`)
   - Vertex buffers / Index buffers (`Mesh.cpp/.h`)
   - Shader compile & bind (`Shader.cpp/.h`)
   - Constant buffers / resource management
   - Vertex Shader / Pixel Shader / Compute Shader (`.hlsl`)
   - Render passes / pipeline state
4. Read the relevant roadmap doc in `docs/roadmap/` before writing any code
5. Check the relevant concept docs in `docs/concepts/`
6. Plan the implementation approach

**Implementation checklist:**
- [ ] HLSL shader compiles without errors (fxc or dxc)
- [ ] C++ host code uses ComPtr<T> for all COM objects
- [ ] Constant buffer structs are 16-byte aligned
- [ ] D3D11/12 debug layer reports no errors or warnings
- [ ] CMake build succeeds (Debug and Release)
- [ ] Shaders are compiled via CMake custom commands
- [ ] Resource cleanup is handled (RAII / explicit Release)

**HLSL Best Practices:**
- Use `cbuffer` with explicit register (`register(b0)`)
- Ensure cbuffer members are 16-byte aligned (use `float2 padding` if needed)
- Prefer `SV_Position` for clip-space output, `SV_Target` for pixel output
- Use `Texture2D` + `SamplerState` with explicit registers (`t0`, `s0`)
- Keep per-frame data in `cbuffer PerFrame : register(b1)`
- Keep per-object data in `cbuffer PerObject : register(b0)`
- For compute shaders, annotate with `[numthreads(X, Y, Z)]`

**D3D11 Best Practices:**
- Use `D3D11_CREATE_DEVICE_DEBUG` during development
- Use `Map/Unmap` with `D3D11_MAP_WRITE_DISCARD` for dynamic CBs
- Prefer `DXGI_SWAP_EFFECT_FLIP_DISCARD` for swap chain
- Always set viewport before draw calls

**D3D12 Best Practices (Phase 5+):**
- Use `ID3D12GraphicsCommandList` for all rendering commands
- Insert resource barriers (`D3D12_RESOURCE_BARRIER`) correctly
- Use descriptor heaps (CBV/SRV/UAV, RTV, DSV)
- Define Root Signature before PSO creation
- Use `WaitForGPU()` pattern for frame synchronization

**Commit Scopes (for later):**
Use these scopes for conventional commits:
- `phase1` / `phase2` / ... / `phase17`: Phase-specific implementation
- `hlsl`: HLSL shader code
- `d3d11`: Direct3D 11 host code
- `d3d12`: Direct3D 12 host code
- `dxr`: DirectX Raytracing
- `compute`: Compute shader code
- `cmake`: Build configuration
- `docs`: Documentation updates
- `chore`: Build, dependencies, tooling

Please proceed with the implementation.
