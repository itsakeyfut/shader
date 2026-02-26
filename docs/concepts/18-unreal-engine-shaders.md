# 18 - Unreal Engine Shaders（UE シェーダー）

## UE マテリアルエディタの仕組み

UE のマテリアルはビジュアルノードグラフで定義され、
コンパイル時に HLSL コード（`.usf`）に変換される。

### マテリアルドメイン
| ドメイン | 用途 |
|---|---|
| Surface | 通常の3Dオブジェクト（最も一般的）|
| Deferred Decal | デカール（サーフェスへの貼り付け）|
| Light Function | 光源の形状・パターン定義 |
| Post Process | ポストプロセスエフェクト |
| UI | HUD・ウィジェット |

### Shading Model
| モデル | 説明 |
|---|---|
| Default Lit | 標準 PBR（Metalness/Roughness）|
| Unlit | ライティングなし |
| Subsurface | 表面下散乱（肌・蝋）|
| Two Sided Foliage | 両面葉（光の透過）|
| Eye | 眼球専用モデル |
| Toon | セルシェーディング |
| Substrate | UE 5.2+ の新マテリアルシステム |

---

## Custom Expression ノード（インライン HLSL）

マテリアルグラフに直接 HLSL コードを埋め込む。

```hlsl
// Custom ノードの Code フィールドに記述
// Input: ピン名を変数として使用（Input0, Input1...またはカスタム名）
// Return: 出力値の型

// 例: 角度から色を生成
float3 dir = normalize(float3(Input0.xy, 0.0));
float angle = atan2(dir.y, dir.x);
return float3(frac(angle / 6.2832), 1.0, 1.0);
```

**制限**:
- デバッグが難しい（RenderDoc での確認が必要）
- ノード化されたコードの最適化をコンパイラが一部行えない
- Lumen / Nanite の一部機能とは非互換の場合あり

---

## .ush / .usf ファイルの作成・インクルード

`/Engine/Shaders/` 以下に置かれた HLSL ファイル（`.ush` = header、`.usf` = shader）。

### プロジェクト用シェーダーの追加
```
MyProject/Shaders/
  Private/
    MyCustomShader.usf
  Public/
    MyShaderCommon.ush
```

`MyProject.Build.cs` に追加:
```csharp
PrivateIncludePaths.Add(Path.Combine(PluginDirectory, "Shaders"));
```

### Custom ノードからインクルード
```hlsl
// Custom ノードの Code フィールド
#include "/MyProject/MyShaderCommon.ush"
return MyCustomFunction(Input0);
```

### グローバルシェーダー（C++ から起動）
```cpp
// C++ 側で宣言
class FMyComputeShader : public FGlobalShader {
    DECLARE_GLOBAL_SHADER(FMyComputeShader);
    SHADER_USE_PARAMETER_STRUCT(FMyComputeShader, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_UAV(RWTexture2D<float4>, OutputTexture)
        SHADER_PARAMETER(FVector2f, InvOutputSize)
    END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FMyComputeShader, "/MyProject/MyComputeShader.usf", "MainCS", SF_Compute);

// 実行
TShaderMapRef<FMyComputeShader> ComputeShader(View.ShaderMap);
FMyComputeShader::FParameters* PassParameters = GraphBuilder.AllocParameters<FMyComputeShader::FParameters>();
PassParameters->OutputTexture = ...;
FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("MyPass"), ComputeShader, PassParameters, FIntVector(...));
```

---

## Vertex Factory

カスタム頂点フォーマットを UE のシェーダーパイプラインに統合する仕組み。
GPU スキニング・インスタンシング・手続き生成頂点などで使用。

### 最小構成

```cpp
// MyVertexFactory.h
class FMyVertexFactory : public FVertexFactory {
    DECLARE_VERTEX_FACTORY_TYPE(FMyVertexFactory);
public:
    // 頂点宣言（Input Layout）
    static void ModifyCompilationEnvironment(
        const FVertexFactoryShaderPermutationParameters& Parameters,
        FShaderCompilerEnvironment& OutEnvironment);

    void InitRHI(FRHICommandListBase& RHICmdList) override;
};

// MyVertexFactory.usf
float4 VertexFactoryGetWorldPosition(FVertexFactoryInput Input, FVertexFactoryIntermediates Intermediates) {
    return TransformLocalToTranslatedWorld(float4(Input.Position.xyz, 1.0));
}
```

---

## シェーダー置換・Permutation

UE はシェーダーを大量のバリアントとしてコンパイルする（Permutation）。

```cpp
// シェーダー特性定義
class FMyShader : public FGlobalShader {
    // ブール型のパーミュテーション次元
    class FEnableFeature : SHADER_PERMUTATION_BOOL("ENABLE_FEATURE");

    using FPermutationDomain = TShaderPermutationDomain<FEnableFeature>;
};
```

```hlsl
// .usf 側
#if ENABLE_FEATURE
    // 機能が有効な場合のコード
#endif
```

**パーミュテーション管理**:
- 次元が増えるほどコンパイル数が指数増加
- 不要なパーミュテーションは `ShouldCompilePermutation` で除外

---

## Substrate（UE 5.2+）マテリアルモデル

従来の固定 Shading Model を置き換える、物理的に正確な積層マテリアルシステム。

```
Substrate マテリアルの概念:
  Slab BSDF （1層分の素材）
    ↓ Mix / Over / Add
  複数の Slab を積層
    ↓
  最終的な BSDF

例: 車のボディ = コーティング層 (Clearcoat Slab)
                 + ペイント層 (Metallic Slab)
                 + プラスチック下地 (Diffuse Slab)
```

```hlsl
// Substrate BSDF ノード（マテリアルグラフ）
// 入力: BaseColor, Roughness, Anisotropy, EmissiveColor...
// 出力: FSubstrateBSDF 構造体

// C++ / .usf での直接操作
FSubstrateBSDF BSDF = CreateSubstrateSlab(BaseColor, Metallic, Roughness, ...);
```

---

## Lumen / Nanite / TSR との連携

### Lumen（Global Illumination）
```
マテリアルの互換性:
  ✅ Default Lit, Subsurface, Eye
  ❌ Unlit（GI に参加しない）
  ⚠️ Translucent（制限あり）

最適化:
  - マテリアルの複雑さを下げる（Lumen は多数のシェーダー変種をコンパイル）
  - World Position Offset は Lumen Ray Tracing に注意が必要
```

### Nanite（Virtualized Geometry）
```
対応マテリアル:
  ✅ Opaque（不透明）、Masked（アルファカットアウト）
  ❌ Translucent、Two Sided Foliage（一部制限）
  ❌ World Position Offset（WPO は非対応、UE5.3+で限定対応）
  ❌ Pixel Depth Offset
```

### TSR（Temporal Super Resolution）
```
マテリアルでの対応:
  - Velocity Output（モーションベクター）を正しく出力すること
  - World Position Offset を使う場合は Velocity 出力に追加のロジックが必要
```

---

## Path Tracer との統合

UE5 の内蔵 Path Tracer（参照レンダラー）との互換性確保。

```cpp
// パストレーサー向けシェーダーバリアントの有効化
// Build.cs
GraphicsDevice->AddShaderSourceDirectoryMapping(
    TEXT("/Engine/Private"), FPaths::EngineDir() / TEXT("Shaders/Private"));

// .usf での PATH_TRACER 定義チェック
#if PATH_TRACER
    // パストレーサー専用コード
#else
    // リアルタイムレンダリング用コード
#endif
```

---

## デバッグ・開発ワークフロー

```
1. r.ShaderDevelopmentMode=1 （ShaderDevelopmentMode をコンソールで有効化）
   → シェーダーコンパイルエラーを詳細に表示

2. stat shaderbatch → シェーダー統計
   stat gpu          → GPU タイム

3. RenderDoc + UE プラグイン → フレームキャプチャ
   （プラグイン: RenderDocPlugin）

4. Shader Complexity View Mode（ビューモード > シェーダー複雑度）
   → 赤いほどシェーダー命令が多い

5. r.ForceDebugViewModes=1 → デバッグビューモードの強制有効化
```

---

## 関連ドキュメント
- [01-hlsl-fundamentals.md](01-hlsl-fundamentals.md) - HLSL の基礎
- [08-pbr-theory.md](08-pbr-theory.md) - UE マテリアルの物理モデル
- [19-slang.md](19-slang.md) - UE での Slang 活用（将来）
- [../roadmap/overview.md](../roadmap/overview.md) - Phase 8: Unreal Integration
