# understand-deep - Comprehensive Codebase Analysis

## Overview

Provides **comprehensive and detailed** analysis of specific files, classes, or features with extensive documentation, code examples, and dependency graphs. Use this for deep dives into complex implementations.

For quick overview, use the `understand` command instead.

## Usage

```bash
understand-deep <target>
```

**target** can be any of:
- **File path**: `hello-triangle/src/D3DApp.cpp`
- **Module name**: `Mesh`, `Shader`, `D3DApp`
- **HLSL file**: `shaders/vertex.hlsl`, `shaders/common.hlsli`
- **Feature name**: `shadow-map`, `pbr-pipeline`, `compute-particles`

## Output Format

Generates detailed documentation in Markdown format containing:

### 1. Overview Section
- **Purpose**: What this feature/module does
- **Responsibility**: Role within the system
- **Related Phase**: Which roadmap phase this belongs to

### 2. Type Definitions Section
- **Class/Struct definitions**: All fields and their types
- **HLSL cbuffer/struct layouts**: With register and semantic annotations
- **Access modifiers**: public/private/protected distinction

### 3. Method / Function Section
- **Method list**: All method signatures
- **Method classification**: Init, Update, Render, Cleanup, Helpers
- **Important methods**: Highlighted methods to understand

### 4. Dependencies Section
- **Includes**: What this file includes
- **Usage locations**: Where this file's classes/functions are used
  - File paths and context
  - Common usage patterns
- **Dependency graph**: ASCII art visualization

### 5. Usage Examples Section
- **Real code examples**: Actual usage extracted from codebase
- **HLSL snippet**: Shader register bindings, cbuffer usage
- **CMake integration**: How shaders are compiled in the build

### 6. Related Files Section
- **Related modules**: Files to understand together
- **Documentation**: Related docs in docs/ directory
- **Next files to read**: Recommended path to deepen understanding

## Processing Steps

1. **Identify target**
   - Search for target file using Glob (`*.cpp`, `*.h`, `*.hlsl`, `*.hlsli`)
   - Display list for confirmation if multiple candidates exist

2. **Parse file**
   - Read target file with Read tool
   - Parse classes, structs, functions (C++) or cbuffers, structs, functions (HLSL)
   - Extract comments and semantics

3. **Analyze dependencies**
   - Search for class/function names across all files using Grep
   - Identify usage in `#include`, field types, function arguments
   - For HLSL: find `#include "common.hlsli"` chains

4. **Extract usage examples**
   - Extract code examples from actual usage locations
   - Find corresponding HLSL `cbuffer` and C++ `Map/Unmap` pairs
   - Find CMake `add_shader` invocations

5. **Generate ASCII diagram**
   - Visualize dependencies in ASCII art graph format
   - Show C++ → HLSL binding relationships
   - Use box drawing characters for clear visualization

6. **Output Markdown**
   - Detailed documentation organized by section
   - Proper syntax highlighting (`cpp`, `hlsl`, `cmake` code blocks)
   - Embedded ASCII diagrams

7. **Save to file**
   - Create `.claude/tmp/` directory if it doesn't exist
   - Save output to `.claude/tmp/understand-deep_<target>.md`
   - Sanitize target name for filename (replace `/`, `::`, spaces with `_`)
   - Display file path to user after completion

## Example Output

```markdown
# `D3DApp` - D3D11 Application Base Class

## Overview

**Purpose**: Encapsulates Direct3D 11 device initialization, swap chain management, and the main render loop

**Responsibilities**:
- Create D3D11 device + context + swap chain
- Manage RTV (Render Target View) and viewport
- Provide virtual Update/Render hooks for derived classes
- Handle window resize events

**Phase**: Phase 1 (hello-triangle) → extended through Phase 4

---

## Type Definitions

### `D3DApp` Class

```cpp
class D3DApp {
public:
    bool Init(HWND hwnd, int width, int height);
    void OnResize(int width, int height);
    virtual void Update(float dt) = 0;
    virtual void Render()        = 0;
    void Shutdown();

protected:
    ComPtr<ID3D11Device>           mDevice;
    ComPtr<ID3D11DeviceContext>    mContext;
    ComPtr<IDXGISwapChain>         mSwapChain;
    ComPtr<ID3D11RenderTargetView> mRTV;
    ComPtr<ID3D11DepthStencilView> mDSV;
    D3D11_VIEWPORT                 mViewport;
    int mWidth, mHeight;
};
```

---

## Methods

### Initialization

```cpp
bool Init(HWND hwnd, int width, int height);  // Create device + swap chain + RTV
void Shutdown();                               // Release all COM objects
```

### Per-frame

```cpp
virtual void Update(float dt) = 0;  // Override: update game state
virtual void Render()        = 0;  // Override: submit draw calls
```

### Window

```cpp
void OnResize(int width, int height);  // Recreate RTV on WM_SIZE
```

---

## Dependencies

### What This File Includes

```cpp
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>   // ComPtr<T>
#include <DirectXMath.h>
```

### Where This File's Types Are Used

- `main.cpp`: Instantiation, message loop, WM_SIZE handling
- `HelloTriangleApp.cpp`: Derived class implementing Update/Render

### Dependency Graph

```
┌─────────────────────────────────────────────────────────────┐
│                       D3DApp.cpp/.h                          │
│                  (D3D11 Application Base)                    │
└────────┬────────────────────────────────┬──────────────────┘
         │                                │
         │ Includes                       │ Used by
         │                                │
    ┌────▼────────────────┐         ┌────▼──────────────────────┐
    │ d3d11.h             │         │ main.cpp                  │
    │ dxgi.h              │         │  - Instantiation          │
    │ wrl/client.h        │         │  - Message loop           │
    │ DirectXMath.h       │         └───────────────────────────┘
    └─────────────────────┘
                                    ┌───────────────────────────┐
                                    │ HelloTriangleApp.cpp      │
                                    │  - Derived: Update/Render │
                                    └───────────────────────────┘
```

---

## Usage Examples

### Initialization in main.cpp

```cpp
D3DApp* app = new HelloTriangleApp();
if (!app->Init(hwnd, 1280, 720)) return -1;

// Message loop
MSG msg = {};
while (msg.message != WM_QUIT) {
    if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    } else {
        app->Update(deltaTime);
        app->Render();
    }
}
app->Shutdown();
```

### Corresponding HLSL (cbuffer alignment)

```hlsl
cbuffer PerFrame : register(b1) {
    float  time;
    float  deltaTime;
    float2 padding;    // 16-byte alignment
};
```

---

## Related Files

### Related Modules to Understand

1. **Mesh** (`Mesh.cpp/.h`) - Vertex/Index buffer management
2. **Shader** (`Shader.cpp/.h`) - Shader compile & pipeline state
3. **shaders/vertex.hlsl** - Vertex shader bound by D3DApp subclass

### Related Documentation

- [docs/roadmap/01-hello-triangle.md](../../docs/roadmap/01-hello-triangle.md)
- [docs/concepts/00-graphics-pipeline.md](../../docs/concepts/00-graphics-pipeline.md)

### Next Files to Read

- **Understand shaders**: `shaders/vertex.hlsl` → `shaders/pixel.hlsl`
- **Add geometry**: `Mesh.cpp/.h`
- **Bind resources**: `Shader.cpp/.h`

---

**Generated**: 2026-02-28
**Command**: `understand-deep D3DApp`
```

## Output File

After generating the documentation, save it to:
```
.claude/tmp/understand-deep_<sanitized_target>.md
```

**Filename sanitization rules**:
- Replace `/` with `_` (e.g., `hello-triangle/src/D3DApp.cpp` → `understand-deep_hello-triangle_src_d3dapp_cpp.md`)
- Replace `::` with `_`
- Replace spaces with `_`
- Convert to lowercase
- Remove special characters except `_` and `-`

**Examples**:
- `understand-deep D3DApp` → `.claude/tmp/understand-deep_d3dapp.md`
- `understand-deep vertex.hlsl` → `.claude/tmp/understand-deep_vertex_hlsl.md`
- `understand-deep shadow-map` → `.claude/tmp/understand-deep_shadow-map.md`

## Important Notes

- **Accuracy First**: Accurately reflect actual code behavior
- **Real Examples**: Learn from actual code, not theory
- **Comprehensive**: Don't miss any important usage locations
- **Readable**: Write in a way developers new to D3D can understand
- **Up-to-date**: Reflect the current state of the codebase
- **Save Output**: Always save the complete documentation to `.claude/tmp/understand-deep_*.md`

## Technical Implementation Guidelines

### Search Strategy

1. **Glob search**: Search by file name pattern
   - `<phase>/**/*.cpp`, `<phase>/**/*.h`
   - `<phase>/shaders/**/*.hlsl`, `<phase>/shaders/**/*.hlsli`

2. **Grep search**: Content-based search
   - `class ${target}` / `struct ${target}` - Class/struct definitions (C++)
   - `cbuffer ${target}` - Constant buffer definitions (HLSL)
   - `#include.*${target}` - Include statements
   - `${target}::` / `new ${target}` - Usage locations

3. **Identify C++/HLSL binding pairs**:
   - Find `cbuffer` in `.hlsl` → match with `Map/Unmap` or `UpdateSubresource` in `.cpp`
   - Find `register(b0)` → match with `VSSetConstantBuffers(0, ...)` in `.cpp`
   - Find `register(t0)` → match with `PSSetShaderResources(0, ...)` in `.cpp`

### ASCII Diagram Generation Rules

```
┌─────────────────────────────────────────────────────────────┐
│                      target.cpp/.h                           │
│                   (Target Module)                            │
└────────┬────────────────────────────────┬──────────────────┘
         │                                │
         │ #include (dependencies)        │ Used by
         │                                │
    ┌────▼────────────────┐         ┌────▼──────────────────────┐
    │ d3d11.h             │         │ main.cpp                  │
    │ DirectXMath.h       │         │  - Context                │
    └─────────────────────┘         └───────────────────────────┘

                                    ┌───────────────────────────┐
                                    │ shaders/vertex.hlsl       │
                                    │  (HLSL side binding)      │
                                    └───────────────────────────┘
```

### File Patterns to Search

```
*.cpp, *.h, *.hpp   → C++ host code
*.hlsl              → HLSL shader entry points
*.hlsli             → HLSL shared includes/headers
CMakeLists.txt      → Build + shader compile rules
```

---

**Purpose**: Help developers deeply understand D3D/HLSL code — including the CPU-GPU boundary (C++ host ↔ HLSL shader binding).
