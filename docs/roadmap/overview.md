# Shader Learning Roadmap - Overview

## 学習方針

Unreal Engine でのゲーム開発 + ゲームエンジン自作を最終目標とした HLSL シェーダー学習。
段階的にプロジェクトを積み上げ、最終的に Slang へ移行する。

---

## フェーズ一覧

| Phase | プロジェクト | API | 主要習得スキル |
|---|---|---|---|
| 1 | `hello-triangle` | D3D11 | GPU パイプライン・HLSL 基礎・頂点変換・テクスチャ |
| 2 | `lighting` | D3D11 | Blinn-Phong・法線・Shadow Map・PCF・CSM・SSAO |
| 3 | `pbr` | D3D11/12 | Cook-Torrance BRDF・IBL・Irradiance/PrefilterMap・BRDF LUT |
| 4 | `post-process` | D3D11/12 | RTT・HDR・Bloom・TAA・DoF・Motion Blur・Color Grading |
| 5 | `advanced-rendering` | D3D12 | Deferred Shading・Forward+・Clustered・SSR・Volumetric |
| 6 | `compute-effects` | D3D12 | GPU Particle・Bitonic Sort・GPU Skinning・Morph・Cloth |
| 7 | `ray-tracing` | D3D12 DXR | BLAS/TLAS・RTAO・RT Shadow・RT Reflection・Path Tracing |
| 8 | `character-rendering` | D3D12 | Skin SSS・Hair・Eye・Cloth・Fur |
| 9 | `environment-rendering` | D3D12 | Terrain・Water・FFT Ocean・Atmosphere・Volumetric Cloud・Foliage |
| 10 | `vfx-rendering` | D3D12 | GPU Particle 発展・炎・爆発・デカール・レンズフレア |
| 11 | `render-graph` | D3D12 | Render Graph・Transient Alloc・Async Compute・Bindless・GPU Driven |
| 12 | `unreal-integration` | UE5 | Global Shader・Vertex Factory・Custom ShadingModel・Niagara |
| 13 | `slang-rewrite` | Slang | HLSL → Slang・Interface・Generics・Auto-Diff・クロスプラットフォーム |
| 14 | `global-illumination` | D3D12 DXR | SH Probe・DDGI・VXGI・Screen Space GI・ReSTIR GI・Lumen 風ハイブリッド |
| 15 | `debug-rendering` | D3D11/12 | Immediate Mode デバッグ描画・AABB/骨格/Gizmo・オーバーレイモード・GPU タイミング |
| 16 | `advanced-shadows` | D3D12 | EVSM・Moment Shadow Map・Virtual Shadow Map・Contact Shadow・Area Light Shadow |
| 17 | `mobile-optimization` | D3D11/Vulkan | TBDR アーキテクチャ・half 精度・ASTC・帯域幅削減・UE5 Mobile |

---

## 前提知識マップ

```
Phase 1 (hello-triangle)
  必要: C++基礎、線形代数（行列・ベクトル）
  習得: [00] [01] [02] [03] [05]の基礎

Phase 2 (lighting)
  必要: Phase 1 完了
  習得: [04] [06] [10] [13]

Phase 3 (pbr)
  必要: Phase 2 完了
  習得: [07] [08] [09]

Phase 4 (post-process)
  必要: Phase 3 完了
  習得: [11] [12] [13の発展]

Phase 5 (advanced-rendering)
  必要: Phase 4 完了、D3D12 基礎
  習得: [14] [15の基礎] [16の一部]

Phase 6 (compute-effects)
  必要: Phase 5 完了
  習得: [15完全] [16]

Phase 7 (ray-tracing)
  必要: Phase 5-6 完了、DXR の概念
  習得: [14の発展] [16の発展]

Phase 8 (character-rendering)
  必要: Phase 3（PBR）・Phase 6（GPU Skinning）完了
  習得: [04の発展] [08の発展] SSS・Hair・Eye 特化技術

Phase 9 (environment-rendering)
  必要: Phase 5・Phase 6 完了
  習得: [15] [17] Terrain/Water/Atmosphere 特化技術

Phase 10 (vfx-rendering)
  必要: Phase 6（GPU Particle）・Phase 5（OIT・Decal）完了
  習得: [14の発展] [15の発展] [17]

Phase 11 (render-graph)
  必要: Phase 5-10 完了（全ての個別パスの実装経験）
  習得: [15] [16] D3D12 深層・エンジンアーキテクチャ

Phase 12 (unreal-integration)
  必要: Phase 3-4 完了（UE は独立して学習可能）
  習得: [18] UE5 固有のAPI・レンダリングパイプライン

Phase 13 (slang-rewrite)
  必要: Phase 1-11 完了（移植元の HLSL コードが存在すること）
  習得: [19] 言語設計・クロスプラットフォーム

Phase 14 (global-illumination)
  必要: Phase 7（DXR）・Phase 9（Probe 概念）・Phase 11（Render Graph）完了
  習得: [20] SH・DDGI・VXGI・ReSTIR GI

Phase 15 (debug-rendering)
  必要: Phase 1 完了（Phase 2〜3 と並行して着手推奨）
  習得: [22] Immediate Mode 描画・GPU タイムスタンプ

Phase 16 (advanced-shadows)
  必要: Phase 2（Shadow 基礎）・Phase 5（Deferred）・Phase 7（DXR）完了
  習得: [10の発展] EVSM・MSM・Virtual Shadow Map・Area Light Shadow

Phase 17 (mobile-optimization)
  必要: Phase 1-4 完了・UE5 経験（Phase 12）
  習得: [00の発展] TBDR・[01の発展] half precision・[21] 帯域幅最適化
```

---

## API バックエンド選択

### Phase 1-4: Direct3D 11
```
理由:
  - シンプルな即時モード API
  - 概念（パイプライン・シェーダー・バッファ）の学習に集中できる
  - デバッグが容易（D3D11 デバッグレイヤー）
  - DXGI / HLSL の基礎は D3D12 でも同じ

参照: Microsoft Learn D3D11 チュートリアル
      DirectX Tool Kit (DirectXTK)
```

### Phase 5+: Direct3D 12
```
理由:
  - 明示的なリソース管理・バリア
  - GPU 駆動描画（DrawIndirect / Indirect Dispatch）
  - Mesh Shader / Amplification Shader
  - DXR（DirectX Raytracing）
  - D3D12Ultimate 機能

参照: DirectX-Graphics-Samples (GitHub)
      Frank Luna "Introduction to 3D Game Programming with DirectX 12"
```

---

## プロジェクトディレクトリ規約

```
D:/dev/shader/
├── docs/                     ← 全プロジェクト共通ドキュメント（本ファイル群）
├── hello-triangle/           ← Phase 1
│   ├── CMakeLists.txt
│   ├── src/
│   │   ├── main.cpp
│   │   ├── D3DApp.cpp/.h
│   │   └── ...
│   └── shaders/
│       ├── vertex.hlsl
│       └── pixel.hlsl
├── lighting/                 ← Phase 2
├── pbr/                      ← Phase 3
└── ...
```

---

## シェーダービルド規約

### fxc（Phase 1-2、Shader Model 5.x）
```cmake
# CMake でのシェーダーコンパイル
function(add_shader TARGET SHADER ENTRY TYPE)
    set(OUTPUT "${CMAKE_BINARY_DIR}/shaders/${SHADER}.cso")
    add_custom_command(
        OUTPUT  ${OUTPUT}
        COMMAND fxc /nologo /T ${TYPE}_5_0 /E ${ENTRY} /Fo ${OUTPUT}
                    $<IF:$<CONFIG:Debug>,/Zi /Od,/O3>
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}.hlsl"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}.hlsl"
        VERBATIM
    )
    set_source_files_properties(${OUTPUT} PROPERTIES GENERATED TRUE)
    target_sources(${TARGET} PRIVATE ${OUTPUT})
endfunction()
```

### dxc（Phase 3+、Shader Model 6.x）
```cmake
function(add_shader_dxc TARGET SHADER ENTRY TYPE)
    set(OUTPUT "${CMAKE_BINARY_DIR}/shaders/${SHADER}.dxil")
    add_custom_command(
        OUTPUT  ${OUTPUT}
        COMMAND dxc -nologo -T ${TYPE}_6_5 -E ${ENTRY} -Fo ${OUTPUT}
                    $<IF:$<CONFIG:Debug>,-Zi -Od,>
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}.hlsl"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}.hlsl"
        VERBATIM
    )
    target_sources(${TARGET} PRIVATE ${OUTPUT})
endfunction()
```

---

## フェーズ概要（Phase 5-13）

### Phase 5: Advanced Rendering
- D3D12 への移行（コマンドリスト・バリア・ディスクリプタヒープ）
- G-Buffer を使った Deferred Shading、Oct-Encoded Normal
- Forward+ のタイルベースライトカリング（CS）
- Clustered Shading（3D タイル）
- SSR（Screen Space Reflections）
- Volumetric Fog（Froxel ベース）
- Deferred Decals、OIT

### Phase 6: Compute Effects
- GPU パーティクルシステム（Emit/Update/Sort/Draw、Dead List 管理）
- Bitonic Sort（半透明ソート）
- DrawIndirect / ExecuteIndirect
- GPU スキニング（LBS / Dual Quaternion）
- モーフターゲット（Blend Shapes）
- GPU クロスシミュレーション（PBD）
- Async Compute の活用

### Phase 7: Ray Tracing
- DXR セットアップ（BLAS/TLAS/SBT/StateObject）
- RTAO（Ray Traced Ambient Occlusion）
- RT シャドウ（エリアライトのソフトシャドウ）
- RT 反射（1 バウンス）
- パストレーシング（多バウンス、Russian Roulette、MIS）
- SVGF デノイザー（空間・時間フィルタリング）
- ハイブリッドレンダリング（Rasterization + RT の合成）

### Phase 8: Character Rendering
- Pre-Integrated Skin SSS（曲率 LUT）
- Separable SSS（スクリーン空間 SSS、Jorge Jimenez）
- Hair Shading（Kajiya-Kay / Marschner 近似）
- 眼球シェーダー（角膜視差・虹彩・強膜 SSS）
- 布シェーダー（Charlie Sheen / Velvet NDF）
- ファー（Shell Rendering）

### Phase 9: Environment Rendering
- 地形テッセレーション（CD-LOD + HS/DS）
- テクスチャスプラッティング（Height-Blend / TriPlanar）
- 水面（Gerstner 波・Fresnel・屈折バッファ）
- FFT 海洋シミュレーション（Phillips スペクトル）
- 大気散乱（Rayleigh/Mie、Bruneton モデル）
- ボリューメトリッククラウド（3D Noise Ray Marching）
- 植生レンダリング（Two-Sided Foliage・Wind）
- 草（Mesh Shader ブレード生成）

### Phase 10: VFX Rendering
- 高度な GPU パーティクル（Curl Noise・Screen Space Collision）
- フリップブックアニメーション（クロスフェード）
- ソフトパーティクル（深度フェード）
- 体積炎シェーダー（Ray Marching + 温度 → 色マップ）
- 爆発・ショックウェーブ（UV 歪み）
- Deferred Decals（G-Buffer 上書き）
- レンズフレア・Chromatic Aberration

### Phase 11: Render Graph
- Frame Graph の設計（DAG・トポロジカルソート）
- Transient リソース管理（Aliasing ヒープ）
- 自動 Resource Barrier 挿入（Split Barrier）
- Async Compute スケジューリング
- Bindless レンダリング（SM 6.6 ResourceDescriptorHeap）
- マテリアルシステム・Permutation 管理
- シェーダーホットリロード
- GPU 駆動描画（Multi-Draw Indirect）

### Phase 12: Unreal Integration
- Custom Expression ノード（インライン HLSL）
- .ush / .usf カスタムシェーダーライブラリ
- Global Shader（RDG ベースのカスタム描画パス）
- カスタム Shading Model（G-Buffer 追加）
- Vertex Factory（カスタム頂点フォーマット）
- Niagara GPU シミュレーション
- Lumen / Nanite / TSR との正しい連携

### Phase 13: Slang Rewrite
- HLSL → Slang への段階的移植
- `import` モジュールシステム
- `IBRDF` / `ILight` インターフェース化
- ジェネリックなレンダリングパス
- Auto-Diff の実験（簡易 NeRF）
- DXIL / SPIR-V / Metal へのクロスコンパイル

### Phase 14: Global Illumination
- Baked SH Probe GI（静的 Irradiance）
- DDGI（Dynamic Diffuse GI）: DXR プローブレイキャスト・Octahedral テクスチャ
- Voxel Cone Tracing（VXGI）: ボクセル化 CS・コーントレーシング
- Screen Space GI（SSGI）: スクリーン空間間接光
- ReSTIR GI: Reservoir ベース高速 GI サンプリング
- Lumen 風ハイブリッド GI: 距離別手法ブレンド

### Phase 15: Debug Rendering
- Immediate Mode デバッグ API（Line / AABB / Sphere / Arrow / Frustum）
- 深度テスト制御（Always-On-Top XRay モード）
- 骨格（Skeleton）可視化
- G-Buffer デバッグオーバーレイモード
- GPU タイムスタンプクエリによるパス別タイミング計測

### Phase 16: Advanced Shadows
- EVSM（Exponential Variance Shadow Map）: Light Bleeding 改善
- Moment Shadow Maps（4 次モーメント）
- Virtual Shadow Map（UE5 方式）: 仮想テクスチャアトラス・ページング
- Contact Shadow（スクリーンスペース微細影）
- Area Light PCSS（半影幅の物理的計算）
- Shadow Atlas 動的管理（Bin Packing・キャッシュ）

### Phase 17: Mobile Optimization
- TBDR アーキテクチャの理解（タイルシェーディング・帯域幅）
- half 精度（min16float）の活用
- ASTC テクスチャ圧縮（4×4 / 6×6 / 8×8）
- フレームバッファ帯域幅削減（DontCare / Framebuffer Fetch）
- UE5 Mobile Rendering パイプライン

---

## 関連ドキュメント

### ロードマップ
- [01-hello-triangle.md](01-hello-triangle.md)
- [02-lighting.md](02-lighting.md)
- [03-pbr.md](03-pbr.md)
- [04-post-process.md](04-post-process.md)
- [05-advanced-rendering.md](05-advanced-rendering.md)
- [06-compute-effects.md](06-compute-effects.md)
- [07-ray-tracing.md](07-ray-tracing.md)
- [08-character-rendering.md](08-character-rendering.md)
- [09-environment-rendering.md](09-environment-rendering.md)
- [10-vfx-rendering.md](10-vfx-rendering.md)
- [11-render-graph.md](11-render-graph.md)
- [12-unreal-integration.md](12-unreal-integration.md)
- [13-slang-rewrite.md](13-slang-rewrite.md)
- [14-global-illumination.md](14-global-illumination.md)
- [15-debug-rendering.md](15-debug-rendering.md)
- [16-advanced-shadows.md](16-advanced-shadows.md)
- [17-mobile-optimization.md](17-mobile-optimization.md)

### 概念ドキュメント
- [concepts/00-graphics-pipeline.md](../concepts/00-graphics-pipeline.md)
- [concepts/08-pbr-theory.md](../concepts/08-pbr-theory.md)
- [concepts/14-advanced-rendering.md](../concepts/14-advanced-rendering.md)
- [concepts/15-compute-shaders.md](../concepts/15-compute-shaders.md)
- [concepts/18-unreal-engine-shaders.md](../concepts/18-unreal-engine-shaders.md)
- [concepts/19-slang.md](../concepts/19-slang.md)
- [concepts/20-global-illumination.md](../concepts/20-global-illumination.md)
- [concepts/21-physical-camera.md](../concepts/21-physical-camera.md)
- [concepts/22-debug-rendering.md](../concepts/22-debug-rendering.md)
