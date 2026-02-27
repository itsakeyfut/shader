# understand - Quick Codebase Overview

## Overview

Provides a **concise, high-level overview** of specific files, structs, or features. Perfect for quick reference and getting oriented.

For comprehensive analysis with detailed examples and dependency graphs, use `understand-deep` instead.

## Usage

```bash
understand <target>
```

**target** can be any of:
- **File path**: `hello-triangle/src/D3DApp.cpp`
- **Module name**: `Mesh`, `Shader`, `D3DApp`
- **Struct/Class name**: `Vertex`, `ConstantBuffer`, `D3DApp`
- **HLSL file**: `shaders/vertex.hlsl`, `shaders/pixel.hlsl`
- **Feature name**: `shadow-map`, `pbr-pipeline`, `compute-particles`

## Output Format

Generates concise documentation in Markdown format (1-2 pages) containing:

### 1. Quick Summary
- **Purpose**: One-liner description
- **Location**: File path
- **Phase**: Implementation phase (from docs/roadmap/)

### 2. Type / Struct Definition
- **Class/Struct signature**: Fields and methods (no full implementation)
- **HLSL cbuffer / struct**: Layout with register annotations

### 3. Key Methods / Functions (Top 5-10)
- **Constructor**: Creation / initialization
- **Core methods**: Most commonly used 3-5 methods
- **Important helpers**: 2-3 utility functions
- Signature only, no implementation details

### 4. Dependencies (Simple List)
- **Direct includes**: 3-5 key headers or HLSL includes
- **Used by**: 3-5 major usage locations
- Simple bullet points only (no diagrams)

### 5. Quick Example
- **One real usage example** from the codebase (5-10 lines)

### 6. Related Files
- **See also**: 2-3 related files to explore
- **Deep dive**: Link to `understand-deep` for full details

## Processing Steps

1. **Identify target**
   - Quick Glob/Grep search (`.cpp`, `.h`, `.hlsl`, `.hlsli`)
   - Confirm if multiple candidates

2. **Extract essentials**
   - Read target file
   - Extract type/struct definition
   - Identify 5-10 most important methods/functions
   - Skip detailed comments

3. **Minimal dependency analysis**
   - List 3-5 direct `#include` / `#include` / `cbuffer` dependencies
   - Find 3-5 main usage locations
   - Skip comprehensive search

4. **Single usage example**
   - Find ONE clear, simple usage example
   - Prefer initialization code or simple draw call setup

5. **Output concise Markdown**
   - Keep to 1-2 pages maximum
   - No diagrams (keep it simple)
   - Focus on "what" not "how"

6. **Save to file**
   - Create `.claude/tmp/` directory if needed
   - Save to `.claude/tmp/understand_<target>.md`
   - Display file path after completion

## Output File

After generating the documentation, save it to:
```
.claude/tmp/understand_<sanitized_target>.md
```

**Examples**:
- `understand D3DApp` → `.claude/tmp/understand_d3dapp.md`
- `understand vertex.hlsl` → `.claude/tmp/understand_vertex_hlsl.md`
- `understand shadow-map` → `.claude/tmp/understand_shadow-map.md`

## Example Output

```markdown
# `D3DApp` - Quick Overview

## Summary

**Purpose**: Manages D3D11 device, swap chain, and render loop
**Location**: `hello-triangle/src/D3DApp.cpp`
**Phase**: Phase 1 (hello-triangle)

---

## Type Definition

```cpp
class D3DApp {
public:
    bool Init(HWND hwnd, int width, int height);
    void OnResize(int width, int height);
    void Update(float dt);
    void Render();
    void Shutdown();

private:
    ComPtr<ID3D11Device>           mDevice;
    ComPtr<ID3D11DeviceContext>    mContext;
    ComPtr<IDXGISwapChain>         mSwapChain;
    ComPtr<ID3D11RenderTargetView> mRTV;
    D3D11_VIEWPORT                 mViewport;
};
```

---

## Key Methods

**Initialization**:
```cpp
bool Init(HWND hwnd, int width, int height);
void Shutdown();
```

**Per-frame**:
```cpp
void Update(float dt);
void Render();
void OnResize(int width, int height);
```

---

## Dependencies

**Includes**:
- `<d3d11.h>` - D3D11 API
- `<dxgi.h>` - DXGI swap chain
- `<wrl/client.h>` - ComPtr<T>

**Used by**:
- `main.cpp` - Instantiation and message loop

---

## Quick Example

```cpp
D3DApp app;
app.Init(hwnd, 1280, 720);
// In message loop:
app.Update(deltaTime);
app.Render();
// On WM_SIZE:
app.OnResize(newWidth, newHeight);
```

---

## Related Files

**See also**:
- `Mesh.cpp/.h` - Vertex buffer management
- `Shader.cpp/.h` - Shader compile & bind
- `shaders/vertex.hlsl` - Vertex shader

**For detailed analysis**: Run `understand-deep D3DApp`

---

**Generated**: 2026-02-28
**Command**: `understand D3DApp`
```

## Important Notes

- **Brevity First**: Keep output to 1-2 pages maximum
- **Skip Details**: No full method implementations, no exhaustive comments
- **Quick Reference**: Optimized for fast lookup, not learning
- **Direct Users**: When more detail is needed, suggest `understand-deep`
- **Save Output**: Always save to `.claude/tmp/understand_*.md`

## Technical Guidelines

### What to Include
- Class/struct signatures (fields, methods)
- HLSL cbuffer layouts and semantic annotations
- Top 5-10 methods/functions only
- 3-5 key `#include` dependencies
- 1 simple usage example
- 2-3 related files

### What to Skip
- Full method implementations
- All methods (just the important ones)
- Exhaustive dependency search
- Multiple code examples
- ASCII diagrams (use understand-deep for those)

### File Patterns to Search

```
*.cpp, *.h, *.hpp   → C++ host code
*.hlsl              → HLSL shader entry points
*.hlsli             → HLSL shared includes/headers
CMakeLists.txt      → Build configuration
```

---

**Purpose**: Provide quick orientation for developers who just need to know "what is this" without deep diving into "how it works".
