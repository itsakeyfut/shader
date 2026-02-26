# References（参考資料）

---

## 書籍・教科書

### リアルタイムレンダリング
- **Real-Time Rendering, 4th Edition** (Akenine-Möller et al.)
  - シェーダー学習の最重要書籍。全フェーズで参照する。
  - https://www.realtimerendering.com/

- **Introduction to 3D Game Programming with DirectX 12** (Frank Luna)
  - D3D12 の実装を丁寧に解説。Phase 5+ の基礎。

- **GPU Pro / GPU Zen シリーズ**
  - シェーダー技法の論文集。各フェーズの発展トピックに。

### 物理ベースレンダリング
- **Physically Based Rendering: From Theory to Implementation** (PBRT, Pharr et al.)
  - PBR の最も詳細な教科書。オンラインで無料公開。
  - https://pbrt.org/

- **GPU Gems 1 / 2 / 3** (NVIDIA)
  - 実用的なシェーダー技法の宝庫。オンラインで無料公開。
  - https://developer.nvidia.com/gpugems/

---

## オンライン学習リソース

### 入門・基礎
- **LearnOpenGL.com**
  - OpenGL だが概念は DirectX と共通。PBR まで網羅。
  - https://learnopengl.com/

- **learnd3d11.blindmindstudio.com**
  - D3D11 入門として分かりやすい日本語コンテンツ

### HLSL・DirectX
- **Microsoft HLSL Documentation**
  - 組み込み関数・セマンティクス・シェーダーモデルの公式リファレンス
  - https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/

- **DirectX-Graphics-Samples (GitHub)**
  - Microsoft 公式のサンプルコード（D3D11/D3D12/DXR）
  - https://github.com/microsoft/DirectX-Graphics-Samples

### プロシージャル・グラフィックス
- **The Book of Shaders** (Patricio Gonzalez Vivo)
  - ノイズ・SDF・手続き生成の入門。インタラクティブで学べる。
  - https://thebookofshaders.com/

- **Inigo Quilez のウェブサイト**
  - SDF・Ray Marching・プロシージャル技法の第一人者
  - https://iquilezles.org/

- **ShaderToy**
  - ブラウザでシェーダーをプロトタイピング。多数のサンプルを参照可能。
  - https://www.shadertoy.com/

### 数式確認
- **Desmos** (https://www.desmos.com/)
  - BRDF・減衰関数などのグラフ確認に便利

---

## SIGGRAPH 論文・スライド

### PBR
- **"Real Shading in Unreal Engine 4"** - Brian Karis, SIGGRAPH 2013
  - Split-Sum Approximation、UE4 PBR の基礎
  - https://cdn2.unrealengine.com/Resources/files/2013SiggraphPresentationsNotes-26915738.pdf

- **"Physically Based Shading at Disney"** - Brent Burley, SIGGRAPH 2012
  - Disney Principled BRDF の原論文
  - https://disneyanimation.com/publications/physically-based-shading-at-disney/

- **"Physically Based Shading in Theory and Practice"** - SIGGRAPH Course 毎年
  - https://blog.selfshadow.com/publications/

### Shadow
- **"Percentage Closer Soft Shadows"** - Randima Fernando, GDC 2005
- **"Cascaded Shadow Maps"** - Wolfgang Engel, ShaderX3

### TAA
- **"Temporal Reprojection Anti-Aliasing in INSIDE"** - Playdead, GDC 2016
- **"High Quality Temporal Supersampling"** - Bryan Karis, SIGGRAPH 2014

### Deferred / Forward+
- **"A Primer On Efficient Rendering Algorithms & Clustered Shading"** - Ola Olsson, 2015
- **"Tiled and Clustered Forward Shading"** - Ola Olsson et al.

---

## シェーダー言語・ツール

### Slang
- **Slang 公式ドキュメント**
  - https://shader-slang.com/slang/user-guide/
- **Slang GitHub**
  - https://github.com/shader-slang/slang
- **Slang Playground** (ブラウザで試せる)
  - https://shader-slang.com/slang-playground/

### コンパイラ
- **dxc (DirectX Shader Compiler)**
  - https://github.com/microsoft/DirectXShaderCompiler
- **fxc**: Windows SDK に含まれる（レガシー SM5.x まで）

### デバッグツール
- **RenderDoc**: フレームキャプチャ・シェーダーデバッグ
  - https://renderdoc.org/
- **PIX for Windows**: GPU パフォーマンス解析
  - https://devblogs.microsoft.com/pix/
- **NVIDIA Nsight Graphics**: NVIDIA GPU 向け詳細プロファイリング

---

## Unreal Engine

- **UE5 Rendering Documentation**
  - https://docs.unrealengine.com/5.3/en-US/rendering-and-graphics-in-unreal-engine/

- **Unreal Engine Shader Development**
  - https://docs.unrealengine.com/5.3/en-US/shader-development-in-unreal-engine/

- **Lumen Technical Details**
  - https://docs.unrealengine.com/5.3/en-US/lumen-technical-details-in-unreal-engine/

- **EpicGames/UnrealEngine (GitHub)** (ライセンス要)
  - Shaders/Private/ 以下の .usf ファイルが参考になる

---

## コミュニティ・フォーラム

- **Graphics Programming Discord** (Shader / GFX の活発なコミュニティ)
- **r/GraphicsProgramming** (Reddit)
- **Computer Graphics StackExchange**
  - https://computergraphics.stackexchange.com/
- **Twitter/X の CG エンジニア** (@t_tertes, @sebastiansylvan など)

---

## 数学の復習

- **3Blue1Brown: Essence of Linear Algebra** (YouTube)
  - 行列・ベクトルの直感的な理解に最適
  - https://www.youtube.com/playlist?list=PLZHQObOWTQDPD3MizzM2xVFitgF8hE_ab

- **Khan Academy: Linear Algebra**
  - https://www.khanacademy.org/math/linear-algebra

---

## 各フェーズ別の優先参照リスト

| フェーズ | 優先参照 |
|---|---|
| Phase 1 (Hello Triangle) | LearnOpenGL Getting Started, DirectX-Graphics-Samples Hello World |
| Phase 2 (Lighting) | LearnOpenGL Lighting, RTR4 Chapter 5-7 |
| Phase 3 (PBR) | Karis 2013, Burley 2012, LearnOpenGL PBR |
| Phase 4 (Post-Process) | SIGGRAPH TAA Papers, RTR4 Chapter 12 |
| Phase 5+ | RTR4 全般, GPU Pro/Zen |
| UE Integration | UE5 公式 Docs, Epic GitHub |
| Slang | Slang 公式 Docs, Slang Playground |
