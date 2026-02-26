# Roadmap: Phase 13 - Slang Rewrite

**プロジェクト**: `D:/dev/shader/slang-rewrite/`
**API**: Slang → DXIL / SPIR-V / Metal
**目標**: Phase 1〜11 で書いた HLSL コードを Slang に移植し、モダンな言語機能で再設計する

---

## 習得する概念

| 概念 | 関連ドキュメント |
|---|---|
| Slang 言語全般 | [19-slang.md](../concepts/19-slang.md) |
| インターフェース駆動設計 | [07-phong-vs-pbr.md](../concepts/07-phong-vs-pbr.md)（BRDF 抽象化の動機）|
| Auto-Diff | [19-slang.md](../concepts/19-slang.md) |

---

## フェーズ分け

### フェーズ 13-1: Slang 環境セットアップと互換性確認

**実装項目**:
- `slangc` のインストール（GitHub Releases から）
- 既存 HLSL ファイルを Slang でそのままコンパイルできるか確認
- CMake への Slang コンパイルステップの追加
- Slang Playground でコードを試す

```cmake
# CMake での Slang コンパイル
function(add_slang_shader TARGET SHADER ENTRY TYPE)
    set(OUTPUT "${CMAKE_BINARY_DIR}/shaders/${SHADER}.dxil")
    add_custom_command(
        OUTPUT  ${OUTPUT}
        COMMAND slangc
                -profile sm_6_5
                -target dxil
                -entry ${ENTRY}
                -stage ${TYPE}
                -o ${OUTPUT}
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}.slang"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}.slang"
        VERBATIM
    )
    target_sources(${TARGET} PRIVATE ${OUTPUT})
endfunction()
```

---

### フェーズ 13-2: モジュールシステムへの移行

HLSL の `#include` を Slang の `import` に置き換える。

**実装項目**:
- `common.hlsli` → `Common.slang`（モジュール定義）
- `import Common;` で依存関係を宣言
- モジュール間の名前空間整理
- 循環依存の解消

```slang
// Common.slang
module Common;

export float3 ReconstructWorldPos(float2 uv, float depth, float4x4 invViewProj) {
    float4 clip  = float4(uv * 2.0 - 1.0, depth, 1.0);
    clip.y       = -clip.y;
    float4 world = mul(invViewProj, clip);
    return world.xyz / world.w;
}
```

```slang
// PBR.slang
module PBR;
import Common;   // 依存関係を明示

export float D_GGX(float NdotH, float roughness) { ... }
export float G_Smith(float NdotV, float NdotL, float roughness) { ... }
export float3 F_Schlick(float cosTheta, float3 F0) { ... }
```

---

### フェーズ 13-3: BRDF インターフェース化

Phase 3 の PBR コードを Slang Interface で抽象化する。

**実装項目**:
- `IBRDF` インターフェースの定義
- `LambertBRDF` / `CookTorranceBRDF` / `HairBRDF` の実装
- ジェネリックなライティングループで統一

```slang
// BRDF.slang
interface IBRDF {
    float3 Evaluate(float3 N, float3 L, float3 V);
    float3 Sample(float3 N, float3 V, float2 Xi, out float pdf);
    float  PDF(float3 N, float3 L, float3 V);
    bool   IsDelta(); // デルタ関数BRDF（完全鏡面など）かどうか
}

struct CookTorranceBRDF : IBRDF {
    float3 albedo;
    float  metalness;
    float  roughness;

    float3 Evaluate(float3 N, float3 L, float3 V) {
        import PBR;
        float3 H    = normalize(L + V);
        float NdotV = max(dot(N, V), 0.001);
        float NdotL = max(dot(N, L), 0.001);
        float NdotH = max(dot(N, H), 0.0);
        float HdotV = max(dot(H, V), 0.0);

        float3 F0 = lerp(float3(0.04), albedo, metalness);
        float  D  = D_GGX(NdotH, roughness);
        float  G  = G_Smith(NdotV, NdotL, roughness);
        float3 F  = F_Schlick(HdotV, F0);

        float3 kD = (1.0 - F) * (1.0 - metalness);
        return (kD * albedo / PI + D * G * F / (4.0 * NdotV * NdotL + 0.0001)) * NdotL;
    }
    float3 Sample(float3 N, float3 V, float2 Xi, out float pdf) { ... }
    float  PDF(float3 N, float3 L, float3 V) { ... }
    bool   IsDelta() { return roughness < 0.001; }
}
```

---

### フェーズ 13-4: ライトインターフェース化

```slang
interface ILight {
    associatedtype SampleType;
    SampleType Sample(float3 hitPos, float2 Xi);
    float3     Radiance(SampleType s, float3 hitPos);
    float      PDF(SampleType s, float3 hitPos);
    bool       IsVisible(SampleType s, float3 hitPos); // シャドウ可視性
}

struct DirectionalLight : ILight {
    float3 direction;
    float3 color;

    struct SampleType { float3 wi; }

    SampleType Sample(float3 hitPos, float2 Xi) {
        return { normalize(-direction) };
    }
    float3 Radiance(SampleType s, float3 hitPos) { return color; }
    float  PDF(SampleType s, float3 hitPos)      { return 1.0; }
    bool   IsVisible(SampleType s, float3 hitPos) {
        // DXR や Shadow Map でテスト
        return TraceShadowRay(hitPos, s.wi, 1e10);
    }
}
```

---

### フェーズ 13-5: ジェネリックなレンダリングパス

```slang
// 任意の BRDF とライトで動作するレンダリングカーネル
float3 EvaluateLighting<TBrdf : IBRDF, TLight : ILight>(
    TBrdf brdf,
    TLight light,
    float3 worldPos,
    float3 N,
    float3 V
) {
    float2 Xi = Hammersley(sampleIndex, totalSamples);
    var sample = light.Sample(worldPos, Xi);

    if (!light.IsVisible(sample, worldPos)) return float3(0);

    float3 L       = sample.wi;
    float3 radiance = light.Radiance(sample, worldPos);
    float3 brdfVal  = brdf.Evaluate(N, L, V);
    float  pdf      = light.PDF(sample, worldPos);

    return brdfVal * radiance / max(pdf, 0.0001);
}

// 使用例
CookTorranceBRDF brdf = { albedo, metalness, roughness };
DirectionalLight light = { sunDir, sunColor };
float3 Lo = EvaluateLighting(brdf, light, worldPos, N, V);
```

---

### フェーズ 13-6: ポストプロセスパスのジェネリック化

```slang
// フルスクリーンパスのテンプレート
interface IPostProcessPass {
    float4 Process(float2 uv, Texture2D inputTex, SamplerState samp);
}

struct ToneMappingPass : IPostProcessPass {
    float exposure;
    float4 Process(float2 uv, Texture2D inputTex, SamplerState samp) {
        float3 hdr = inputTex.Sample(samp, uv).rgb * exposure;
        float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
        float3 ldr = saturate((hdr * (a * hdr + b)) / (hdr * (c * hdr + d) + e));
        return float4(ldr, 1.0);
    }
}

// ピクセルシェーダーエントリポイント
[shader("pixel")]
float4 PostProcessPS<TPass : IPostProcessPass>(
    float2 uv : TEXCOORD0,
    uniform TPass pass
) : SV_Target {
    return pass.Process(uv, inputTexture, linearSampler);
}
```

---

### フェーズ 13-7: Auto-Diff の実験（NeRF 的アプローチ）

**実装項目**:
- `[Differentiable]` 関数の作成
- Forward diff / Backward diff の実行
- 簡易 NeRF（Occupancy Field）の勾配計算
- Slang の `bwd_diff()` を使った最適化ループ

```slang
// 微分可能なレンダリング関数
[Differentiable]
float3 RenderPixel(DifferentialPair<float3> params, Ray ray) {
    // params.p = 現在の値, params.d = 微分
    float3 albedo = float3(params.p.x, params.p.y, params.p.z);
    // ... レイトレーシング計算 ...
    return albedo * lighting;
}

// 逆伝播でパラメータの勾配を計算
void OptimizeParams(float3 targetColor, Ray ray, inout float3 params) {
    // フォワードパス
    var dParams = diffPair(params);
    float3 rendered = RenderPixel(dParams, ray);

    // ロス計算
    float3 loss = rendered - targetColor;

    // バックワードパス（自動微分）
    bwd_diff(RenderPixel)(dParams, ray, loss);

    // 勾配でパラメータを更新（SGD）
    params -= dParams.d * learningRate;
}
```

---

### フェーズ 13-8: クロスプラットフォームコンパイル確認

**実装項目**:
- `slangc -target dxil` (DirectX12)
- `slangc -target spirv` (Vulkan)
- `slangc -target metal` (Apple)
- `slangc -target cuda` (NVIDIA CUDA)
- 各バックエンドでの動作確認とパフォーマンス比較

```bash
# 同一 Slang ソースを複数バックエンドにコンパイル
slangc pbr.slang -entry PSMain -stage fragment -target dxil  -o pbr.dxil
slangc pbr.slang -entry PSMain -stage fragment -target spirv -o pbr.spv
slangc pbr.slang -entry PSMain -stage fragment -target metal -o pbr.metallib
```

---

### フェーズ 13-9: UE5 への Slang 統合（将来）

UE5 は Slang の採用を検討中（Epic / NVIDIA のコラボレーション）。

**調査項目**:
- Slang 公式の UE プラグイン状況の確認
- SPIR-V 経由での Vulkan RHI との連携
- カスタム UE RHI シェーダーコンパイラとしての Slang 統合

---

## ファイル構成（完成時）

```
slang-rewrite/
├── CMakeLists.txt
├── src/
│   ├── SlangSession.cpp/.h      ← Slang API のラッパー
│   └── D3D12App.cpp/.h
└── shaders/
    ├── modules/
    │   ├── Common.slang         ← 共通ユーティリティ
    │   ├── Math.slang
    │   ├── PBR.slang            ← D_GGX, G_Smith, F_Schlick
    │   ├── IBL.slang
    │   ├── Shadows.slang
    │   └── PostProcess.slang
    ├── interfaces/
    │   ├── BRDF.slang           ← IBRDF interface
    │   ├── Lights.slang         ← ILight interface
    │   └── RenderPass.slang     ← IPostProcessPass interface
    ├── materials/
    │   ├── CookTorrance.slang
    │   ├── Hair.slang
    │   └── Cloth.slang
    └── main/
        ├── gbuffer.slang
        ├── lighting.slang
        ├── postprocess.slang
        └── nerf_experiment.slang ← Auto-Diff 実験
```

---

## 確認チェックリスト

- [ ] 既存 HLSL コードが Slang でそのままコンパイルできる
- [ ] `import` モジュールシステムが `#include` と同等に動作する
- [ ] `IBRDF` インターフェースが LambertBRDF / CookTorranceBRDF の両方で動作する
- [ ] 同一 Slang ソースが DXIL と SPIR-V に正しくコンパイルされる
- [ ] Auto-Diff で単純な関数の微分が正しく計算される
- [ ] Slang 版のパフォーマンスが HLSL 版と同等（PIX / RenderDoc で比較）

---

## 関連ドキュメント
- [12-unreal-integration.md](12-unreal-integration.md) - 前フェーズ
- [../concepts/19-slang.md](../concepts/19-slang.md)
- [../references.md](../references.md) - Slang 公式ドキュメントリンク
