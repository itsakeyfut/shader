# 19 - Slang（シェーダー言語 Slang）

## Slang 言語概要

Slang は NVIDIA Research が開発したシェーダー言語。
HLSL を基盤としつつ、モダンな言語機能を追加する。

**コンパイルターゲット**:
- HLSL（DirectX）
- SPIR-V（Vulkan）
- GLSL（OpenGL）
- Metal（Apple）
- CUDA（NVIDIA）
- CPU（デバッグ用）

**公式サイト**: https://shader-slang.com
**GitHub**: https://github.com/shader-slang/slang

---

## HLSL との比較

### 基本構文（ほぼ同じ）
```hlsl
// HLSL
float4 PSMain(float2 uv : TEXCOORD0) : SV_Target {
    return tex.Sample(samp, uv);
}

// Slang（同じコードが動く）
[shader("pixel")]
float4 PSMain(float2 uv : TEXCOORD0) : SV_Target {
    return tex.Sample(samp, uv);
}
```

### Slang の追加機能

| 機能 | HLSL | Slang |
|---|---|---|
| ジェネリクス | なし | `T: IBound` |
| インターフェース | なし | `interface IMaterial { ... }` |
| Associated Types | なし | `associatedtype Output` |
| 自動微分 | なし | `[Differentiable]` |
| モジュールシステム | `#include` のみ | `import` |
| 型推論 | 限定的 | `let x = ...` |

---

## Generics（ジェネリクス）

型パラメータを持つ汎用コードを書ける。

```slang
// 型制約付きジェネリクス
float3 Transform<T: IMatrix>(T mat, float3 v) {
    return mul(mat, float4(v, 1.0)).xyz;
}

// 複数の制約
T Clamp<T: IComparable & IArithmetic>(T val, T lo, T hi) {
    return max(lo, min(hi, val));
}

// 使用例
float3x4 worldMatrix = ...;
float3 worldPos = Transform(worldMatrix, localPos);
```

---

## Interface（インターフェース駆動設計）

BRDF・ライトモデルなどを抽象化できる。

```slang
interface IBRDF {
    // BRDF の評価
    float3 Evaluate(float3 N, float3 L, float3 V);

    // 重点サンプリング
    float3 Sample(float3 N, float3 V, float2 Xi);

    // PDF（確率密度関数）
    float PDF(float3 N, float3 L, float3 V);
}

// 実装
struct LambertBRDF : IBRDF {
    float3 albedo;

    float3 Evaluate(float3 N, float3 L, float3 V) {
        return albedo / PI * max(dot(N, L), 0.0);
    }

    float3 Sample(float3 N, float3 V, float2 Xi) {
        return CosineSampleHemisphere(N, Xi);
    }

    float PDF(float3 N, float3 L, float3 V) {
        return max(dot(N, L), 0.0) / PI;
    }
}

struct CookTorranceBRDF : IBRDF {
    float3 albedo;
    float  metalness;
    float  roughness;

    float3 Evaluate(float3 N, float3 L, float3 V) { ... }
    float3 Sample(float3 N, float3 V, float2 Xi)   { ... }
    float  PDF(float3 N, float3 L, float3 V)        { ... }
}

// ジェネリクスと組み合わせ
float3 PathTrace<TBrdf: IBRDF>(TBrdf brdf, Ray ray) {
    // BRDF の実装に依存しない汎用コード
    float3 L = brdf.Sample(N, V, RandomFloat2(seed));
    float3 f = brdf.Evaluate(N, L, V);
    float  p = brdf.PDF(N, L, V);
    return f * max(dot(N, L), 0.0) / p;
}
```

---

## Associated Types

インターフェースが「型」を要求できる。

```slang
interface ILight {
    // このライトが生成するサンプル型
    associatedtype SampleType;

    // ライトをサンプリング
    SampleType Sample(float3 hitPos, float2 Xi);

    // PDF
    float PDF(SampleType s, float3 hitPos);

    // 放射輝度
    float3 Radiance(SampleType s);
}

struct PointLight : ILight {
    float3 position;
    float3 color;

    struct SampleType {
        float3 lightDir;
        float  dist;
    }

    SampleType Sample(float3 hitPos, float2 Xi) {
        SampleType s;
        s.lightDir = normalize(position - hitPos);
        s.dist     = length(position - hitPos);
        return s;
    }

    float PDF(SampleType s, float3 hitPos) { return 1.0; }
    float3 Radiance(SampleType s) { return color / (s.dist * s.dist); }
}
```

---

## Auto-Diff（自動微分）

機械学習・物理シミュレーション向けに微分を自動計算。

```slang
// [Differentiable] アトリビュートで微分可能関数をマーク
[Differentiable]
float3 RenderImage(DiffTensorView<float> params, Ray ray) {
    // レンダリング計算（自動的に微分可能になる）
    float3 albedo = float3(params[0], params[1], params[2]);
    float3 color  = ShadePixel(ray, albedo);
    return color;
}

// 逆伝播（Backward）の使用
var dParams = DiffTensorView<float>.zeros(3);
bwd_diff(RenderImage)(params, ray, dLoss, dParams);
// dParams に ∂Loss/∂params が入る
```

**用途**:
- ニューラルレンダリング（NeRF, 3D Gaussian Splatting）
- 逆レンダリング（素材・照明推定）
- 物理シミュレーションの最適化

---

## slangc コンパイラと Slang Playground

### slangc（コマンドラインコンパイラ）
```bash
# DXIL（DirectX12向け）に変換
slangc shader.slang -profile sm_6_5 -target dxil -o shader.dxil

# SPIR-V（Vulkan向け）に変換
slangc shader.slang -profile glsl_460 -target spirv -o shader.spv

# HLSL に変換（デバッグ・確認用）
slangc shader.slang -target hlsl -o shader.hlsl

# エントリーポイント指定
slangc shader.slang -entry PSMain -stage fragment -target dxil -o ps.dxil
```

### Slang Playground
ブラウザ上でリアルタイムにSlangコードを試せる。
https://shader-slang.com/slang-playground/

### API 統合（D3D12 例）
```cpp
// Slang セッションの作成
slang::IGlobalSession* globalSession = nullptr;
slang::createGlobalSession(&globalSession);

slang::ISession* session = nullptr;
slang::SessionDesc sessionDesc = {};
sessionDesc.targets = &targetDesc; // DXIL ターゲット
globalSession->createSession(sessionDesc, &session);

// シェーダーのコンパイル
slang::IBlob* diagnostics = nullptr;
slang::IComponentType* program = nullptr;
session->createCompositeComponentType(components, ..., &program, &diagnostics);

// DXIL バイナリを取得
slang::IBlob* code = nullptr;
program->getEntryPointCode(entryPointIndex, 0, &code, &diagnostics);
```

---

## HLSL プロジェクトの段階的 Slang 移植戦略

### フェーズ 1: 互換モードで動かす
Slang は HLSL とほぼ互換性があるため、`.hlsl` を `.slang` にリネームして
そのままコンパイル可能（大部分は変更なし）。

```bash
# 既存の HLSL がそのままコンパイルされることを確認
slangc myShader.hlsl -target hlsl -o myShader.out.hlsl
```

### フェーズ 2: モジュール化
```slang
// 従来: #include のみ
#include "common.hlsli"

// Slang: import モジュール
import CommonLighting;  // CommonLighting.slang を参照
import PBR.CookTorrance; // ディレクトリ構造もOK
```

### フェーズ 3: インターフェース化
ハードコードされた BRDF・ライティングモデルをインターフェースに移行。
新機能追加時に既存コードを変更せずに拡張できる。

### フェーズ 4: ジェネリクス活用
共通処理（トーンマッピング、ブラー等）をジェネリクスで統一。

### フェーズ 5: Auto-Diff（ML 活用）
NeRF・ガウシアンスプラッティング等の学習ベースレンダリングに Auto-Diff を活用。

---

## 移植チェックリスト

- [ ] `slangc` のインストール確認（[GitHub Releases](https://github.com/shader-slang/slang/releases)）
- [ ] 既存 HLSL ファイルを Slang でコンパイルできるか確認
- [ ] `register(b/t/s/u)` → Slang の `ParameterBlock` への移行検討
- [ ] HLSL の `cbuffer` → Slang の `ConstantBuffer<T>` で型安全性向上
- [ ] よく使う関数ライブラリを `interface` + `impl` に移行

---

## 関連ドキュメント
- [01-hlsl-fundamentals.md](01-hlsl-fundamentals.md) - HLSL の基礎（移行前提知識）
- [08-pbr-theory.md](08-pbr-theory.md) - BRDF（インターフェース化の候補）
- [../roadmap/overview.md](../roadmap/overview.md) - Phase 9: Slang Rewrite
