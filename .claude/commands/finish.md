---
description: Commit changes and create PR (keep under 100 lines)
allowed-tools: ["bash", "read", "grep"]
argument-hint: "[file1] [file2] ..."
---

Complete the implementation workflow:

**Steps:**

1. **Check current status:**
   ```bash
   git status
   git diff --stat
   ```

2. **Verify changes are under 100 lines:**
   ```bash
   git diff | wc -l
   ```
   Check the diff size. If over 100 lines, consider splitting into multiple PRs.

3. **MANDATORY: Verify and move to working branch:**

   **CRITICAL**: NEVER commit directly to `main` branch!

   ```bash
   # Check current branch
   git branch --show-current
   ```

   **If currently on `main`:**
   - STOP and create/switch to a feature branch first
   - Example: `git checkout -b feat/phase-01-triangle` or `git checkout existing-branch`

   **If already on a feature branch:**
   - Verify the branch name is correct
   - Proceed to next step

   **Branch naming convention:**
   - `feat/<description>` for features
   - `fix/<description>` for bug fixes
   - `refactor/<description>` for refactoring
   - `docs/<description>` for documentation
   - `chore/<description>` for tooling/build
   - `phase/<number>-<description>` for phase implementation

4. **Run quality checks:**
   ```bash
   # CMake configure + build (Debug)
   cmake -B build -DCMAKE_BUILD_TYPE=Debug
   cmake --build build

   # CMake configure + build (Release)
   cmake -B build-release -DCMAKE_BUILD_TYPE=Release
   cmake --build build-release

   # Shader compile check (fxc, Phase 1-2)
   fxc /nologo /T vs_5_0 /E VSMain shaders/vertex.hlsl
   fxc /nologo /T ps_5_0 /E PSMain shaders/pixel.hlsl

   # Shader compile check (dxc, Phase 3+)
   dxc -nologo -T vs_6_5 -E VSMain shaders/vertex.hlsl
   dxc -nologo -T ps_6_5 -E PSMain shaders/pixel.hlsl
   ```

5. **Stage and commit changes:**

   **File selection:**
   - If specific files were provided as arguments: `$ARGUMENTS`
     → Use: `git add $ARGUMENTS` (commit only specified files)
   - If no arguments were provided:
     → Use: `git add .` (commit all changed files)

   **Commit guidelines:**
   - Create logical, atomic commits
   - Follow conventional commits format: `<type>(<scope>): <description>`
   - Reference issue numbers with "Closes #XXX"
   - Write commit message in English
   - Example: `feat(phase1): implement D3D11 swap chain initialization`

   **Commit Scopes:**
   - `phase1` / `phase2` / ... / `phase17`: Phase-specific implementation
   - `hlsl`: HLSL shader code
   - `d3d11`: Direct3D 11 host code
   - `d3d12`: Direct3D 12 host code
   - `dxr`: DirectX Raytracing code
   - `compute`: Compute shader code
   - `cmake`: CMake build configuration
   - `docs`: Documentation
   - `chore`: Build/tooling

6. **Push changes:**
   ```bash
   git push -u origin <branch-name>
   ```

7. **Create PR using gh command:**
   ```bash
   gh pr create --title "..." --body "..."
   ```

**PR Guidelines:**

**MANDATORY: Write PR in Japanese (日本語で記述)**

**MANDATORY PR Body Limit: MAXIMUM 100 LINES**

- **Keep PR body concise** - MUST be under 100 lines
- Use clear, concise language in Japanese
- Include only essential information:
  - 概要 (Brief summary: 2-4 sentences)
  - 変更内容 (Key changes: 3-5 bullet points)
  - テスト (Test plan: brief checklist)
  - 関連Issue (Related issues: "Closes #XXX")
- Avoid verbose descriptions, excessive formatting, or redundant information

**PR Title:**
- Follow conventional commits format in English
- Include scope if applicable
- Example: `feat(phase1): D3D11初期化とhello-triangle実装`
- Example: `fix(hlsl): 定数バッファのアライメントバグを修正`
- Example: `docs: Phase 2 ロードマップを更新`

**PR Body Template (in Japanese):**
```markdown
## 概要
[2-4 sentences describing the change]

## 変更内容
- [Key change 1]
- [Key change 2]
- [Key change 3]

## テスト
- [ ] CMakeビルド成功（Debug / Release）
- [ ] シェーダーコンパイル成功（fxc / dxc）
- [ ] D3D11デバッグレイヤーでエラーなし
- [ ] RenderDocでパイプライン確認済み

Closes #XXX
```

Please proceed with these steps.
