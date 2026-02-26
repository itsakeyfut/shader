# Roadmap: Phase 17 - Mobile Optimization（モバイル最適化）

**プロジェクト**: `D:/dev/shader/mobile-optimization/`
**API**: Direct3D 11 / Vulkan（Android: Adreno / Mali / Apple GPU）
**目標**: タイルベース GPU のアーキテクチャを理解し、UE5 Mobile でのシェーダー最適化を実践する

> **対象**: UE5 でモバイルゲームの開発・最適化をする場合に必要。デスクトップ専用なら優先度低。

---

## 習得する概念

| 概念 | 関連ドキュメント |
|---|---|
| GPU パイプライン基礎 | [00-graphics-pipeline.md](../concepts/00-graphics-pipeline.md) |
| HLSL 基礎・数値精度 | [01-hlsl-fundamentals.md](../concepts/01-hlsl-fundamentals.md) |
| Compute Shader | [15-compute-shaders.md](../concepts/15-compute-shaders.md) |
| PBR 理論 | [08-pbr-theory.md](../concepts/08-pbr-theory.md) |

---

## モバイル GPU アーキテクチャ（TBDR）

### Desktop GPU（Immediate Mode Rendering: IMR）vs Mobile GPU（TBDR）

```
[Desktop: IMR]
  Draw Call → 全ピクセルを即時 Fragment Shader → フレームバッファ（VRAM）に書き込み
  → Framebuffer は VRAM 上にあるため帯域幅消費が大きい

[Mobile: TBDR - Tile-Based Deferred Rendering]
  1. Geometry Processing: 全三角形の頂点変換のみ実行（Vertex Shading）
  2. Tiling: 画面を 16×16 や 32×32 のタイルに分割
  3. Binning: 各タイルと交差する三角形リストを作成
  4. Fragment Shading: タイルごとに Fragment Shader を実行
     → タイル内フレームバッファは On-Chip SRAM 上（超高速・省電力）
  5. Writeback: 最終結果だけ VRAM に書き込み

メリット:
  - フレームバッファのメモリ帯域幅を大幅削減
  - Hidden Surface Removal (HSR) で隠れたピクセルの Fragment Shader をスキップ
  - Early-Z が自動的に効く

デメリット:
  - タイル間のデータ共有が困難（SSR・Deferred Shading の制約）
  - 大きなジオメトリ（タイルをまたぐ三角形が多い）で Binning コスト増
```

---

## フェーズ分け

### フェーズ 17-1: モバイル GPU の計測と理解

**実装項目**:
- Android GPU Inspector（Snapdragon Profiler / Mali Offline Compiler）の使い方
- Xcode GPU Frame Capture（Metal / Apple GPU）
- フレームバッファ読み書き回数の確認（Bandwidth 計測）
- ALU バウンド / Bandwidth バウンド の見分け方
- テクスチャバンドキット（PowerVRI PVRShaderEditor / ARM Streamline）

```
モバイル最適化の優先順位:
  1. フレームバッファ帯域幅の削減（最重要）
  2. テクスチャメモリ帯域幅の削減（ASTC 圧縮）
  3. ALU 命令数の削減（half 精度活用）
  4. Draw Call 削減（GPU Instancing）
  5. Overdraw 削減（Depth Pre-Pass / Alpha ソート）
```

---

### フェーズ 17-2: half 精度（fp16）の活用

モバイル GPU は fp16 演算がほぼ無料か fp32 の 2 倍速。積極的に活用する。

**実装項目**:
- `min16float` / `half`（HLSL の fp16 型）の使い方
- 精度が必要な計算（ワールド座標・深度）と不要な計算（カラー・法線）の見分け方
- Vulkan の `RelaxedPrecision` デコレーション相当
- 精度不足バグの検出方法（PC の fp32 と Mobile の fp16 の比較）

```hlsl
// half 精度 PBR（モバイル向け）
// ワールド座標（精度要）: float のまま
// カラー・法線（精度不要）: min16float に切り替え

float4 MobilePBRPS(PSInput input) : SV_Target {
    // 高精度が必要: ワールド座標・UV
    float3 worldPos  = input.worldPos;    // float のまま
    float2 uv        = input.uv;          // float のまま

    // 低精度で OK: カラー・法線・ライト計算
    min16float3 albedo   = (min16float3)albedoTex.Sample(s, uv).rgb;
    min16float3 N        = (min16float3)normalize(input.worldNormal);
    min16float  roughness = (min16float)roughnessTex.Sample(s, uv).r;
    min16float  metalness = (min16float)metalnessTex.Sample(s, uv).r;

    min16float3 L = (min16float3)normalize(lightDir);
    min16float  NdotL = (min16float)max(dot(N, L), 0.0);

    // PBR 計算も min16float で
    min16float3 brdf = MobileBRDF(albedo, N, L, (min16float3)normalize(cameraDir - worldPos),
                                   roughness, metalness);
    min16float3 color = brdf * (min16float3)lightColor * NdotL;
    return float4((float3)color, 1.0);
}
```

---

### フェーズ 17-3: ASTC テクスチャ圧縮

モバイル標準のテクスチャ圧縮フォーマット。BC（DXT）の代替として使用。

**実装項目**:
- ASTC ブロックサイズ選択（4×4 / 6×6 / 8×8 / 10×10 / 12×12）
- HDR テクスチャへの ASTC HDR 適用
- `Mali Texture Compression Tool` / `compressonator` でのバッチ変換
- UE5 での ASTC 設定（`DefaultEngine.ini` の Texture グループ設定）
- sRGB vs Linear での ASTC 品質差

```
ASTC ブロックサイズ別の圧縮率と品質:
  4×4  : 8 bpp → 8:1（DXT と同等、最高品質）← Albedo / Normal Map
  6×6  : 3.6 bpp → ~18:1                    ← Roughness / AO
  8×8  : 2 bpp → 32:1                       ← 遠景テクスチャ
  12×12: 0.89 bpp → ~72:1（最低品質）

UE5 テクスチャグループ設定（DefaultEngine.ini）:
  [TextureGroup_World]
  MinLODSize=1
  MaxLODSize=2048
  LODBias=0
  MipGenSettings=TMGS_SimpleAverage
  CompressionSettings=TC_Default    ← PC: BC1/BC3, Mobile: ASTC 6x6

Mobile vs Desktop フォーマット比較:
  Desktop  : BC1(RGB), BC3(RGBA), BC5(RG/Normal), BC6H(HDR)
  Mobile   : ASTC 4x4 / 6x6 / 8x8, ETC2(古い Android)
```

---

### フェーズ 17-4: フレームバッファ帯域幅削減

TBDR の恩恵を最大化するために、不要なフレームバッファの読み書きを排除する。

**実装項目**:
- `DontCare` ロード（Vulkan: `VK_ATTACHMENT_LOAD_OP_DONT_CARE`）
- Resolve のインプレース化（MSAA Resolve をタイル内で完結させる）
- Deferred Shading は**回避**（G-Buffer 書き込み = VRAM 往復 = TBDR の天敵）
- Forward Lighting（ライト数を制限したシングルパス）の採用
- `PixelLocal Storage`（iOS）/ `Framebuffer Fetch`（Android）の活用

```hlsl
// Framebuffer Fetch 拡張（iOS Metal / Vulkan VK_EXT_rasterization_order_attachment_access）
// → タイル内の前のフレームバッファ値を追加帯域なしで読める

// GLSL / Metal 例（概念）
// [[color(0)]] float4 prevColor  ← 前ピクセルのカラーを読む（TBDR でゼロコスト）
// → Deferred Lighting をタイル内で完結させる「Tile Shading」が可能

// HLSL (SM 6.6 での相当): RasterizerOrderedViews (ROV) で近似
RasterizerOrderedTexture2D<float4> accumulationBuffer : register(u0);

float4 TileLightingPS(PSInput input) : SV_Target {
    float4 prev = accumulationBuffer[input.svPos.xy]; // ROV で読む
    float4 curr = ComputeLighting(input);
    return prev + curr; // 加算ブレンドをシェーダー内で手動実装
}
```

---

### フェーズ 17-5: モバイル向けシェーダー最適化

**実装項目**:
- 分岐の削減（`step / smoothstep` による数学的ブランチ除去）
- テクスチャサンプル数の削減（チャンネルパッキング: `R=Roughness, G=Metalness, B=AO`）
- sin/cos の近似（`fast_sin`, テイラー展開）
- normalize の回避（補間後に正規化のタイミングを減らす）
- Loop Unrolling でコンパイラの最適化を助ける

```hlsl
// テクスチャパッキング（モバイル向け）
// 従来: Albedo(RGB) + Roughness + Metalness + AO = 3 テクスチャ (4+1+1+1 ch)
// モバイル: Albedo(RGB) + ORM(Occlusion/Roughness/Metalness) = 2 テクスチャ

float4 ormTex = ormTexture.Sample(s, uv);
float  occlusion = ormTex.r;
float  roughness = ormTex.g;
float  metalness = ormTex.b;

// 分岐を step で除去
// 悪い例
float shadow = (NdotL > 0.0) ? ComputeShadow(worldPos) : 0.0;
// 良い例
float shadow = ComputeShadow(worldPos) * step(0.0, NdotL);

// 安価な pow 近似（gamma correction 用）
float3 LinearToGamma(float3 c) {
    // pow(c, 1/2.2) ≈ sqrt（近似で十分な場合）
    return sqrt(c); // 約 10x faster than pow
}
```

---

### フェーズ 17-6: UE5 Mobile Rendering パイプライン

**実装項目**:
- UE5 の Mobile Rendering モード（`ES3.1 / Vulkan / Metal`）の理解
- Mobile HDR（Gamma space vs Linear space の切り替え）
- Mobile シャドウ（Static Shadow Map のみ vs 動的シャドウ）
- UE5 Mobile Forward Renderer の仕組みと制約
- `r.MobileHDR` / `r.Mobile.ForwardShadingMaxNumPointLights` コンソール変数
- Lumen / Nanite の Mobile での利用可否（制限の理解）

```
UE5 Mobile Rendering チェックリスト:
  ✅ Mobile Forward Renderer（デフォルト）
  ✅ 動的シャドウ（限定的、カスケード数削減）
  ✅ 反射キャプチャ（Sphere Reflection Capture）
  ✅ GPU Particle（ES3.1 以上）
  ✅ ASTC テクスチャ圧縩
  ❌ Lumen（Desktop only）
  ❌ Nanite（Desktop only）
  ⚠️ Deferred Shading（Mobile Deferred: 一部 GPU のみ）
  ⚠️ Ray Tracing（Mobile 非対応）

モバイル向けライト数制限:
  Max Dynamic Lights: 4 〜 8（GPU 依存）
  Cascade Shadow Maps: 1 〜 2 カスケード
  Static/Stationary Light: 任意数（ShadowMap ベイク）
```

---

### フェーズ 17-7: GPU Driven Rendering（モバイル向け）

**実装項目**:
- Indirect Draw でのドローコール削減（モバイルは特に CPU ボトルネックになりやすい）
- Occlusion Culling（Hi-Z ベース CS）
- GPU Instancing の最大活用（フォリッジ・プロップ）
- Draw Call を 100 以下に収める最適化ワークフロー

---

## ファイル構成（完成時）

```
mobile-optimization/
├── CMakeLists.txt
├── src/
│   ├── MobileForwardRenderer.cpp/.h   ← モバイル Forward Renderer
│   ├── MobileShadowSystem.cpp/.h      ← 制約付きシャドウ
│   └── MobileDebugProfiler.cpp/.h    ← 帯域幅・温度計測
└── shaders/
    ├── mobile_pbr_ps.hlsl             ← min16float PBR
    ├── mobile_shadow_ps.hlsl          ← シンプルシャドウ
    ├── mobile_composite_ps.hlsl       ← Framebuffer Fetch 活用
    └── astc_test_ps.hlsl              ← ASTC 品質比較テスト
```

---

## 確認チェックリスト

- [ ] min16float への変更でビジュアルに問題がないことを fp32 と比較確認
- [ ] ASTC 4×4 と 6×6 の品質差を目視確認
- [ ] Snapdragon Profiler / Xcode でフレームバッファ帯域幅の削減量を計測
- [ ] モバイル実機でターゲットフレームレート（30/60fps）を達成
- [ ] UE5 の Mobile Preview でデスクトップとの見た目の差を把握

---

## 関連ドキュメント
- [16-advanced-shadows.md](16-advanced-shadows.md) - 前フェーズ
- [../concepts/00-graphics-pipeline.md](../concepts/00-graphics-pipeline.md)
- [../concepts/01-hlsl-fundamentals.md](../concepts/01-hlsl-fundamentals.md)
- [12-unreal-integration.md](12-unreal-integration.md) - UE5 統合（前提）
