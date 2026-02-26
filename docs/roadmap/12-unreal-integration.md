# Roadmap: Phase 12 - Unreal Engine Integration

**プロジェクト**: UE5 プロジェクト（`D:/dev/ue/MyShaderProject/` など）
**API**: Unreal Engine 5 + HLSL（.usf）
**目標**: UE5 のレンダリングパイプラインを深く理解し、カスタムシェーダーを実用レベルで活用する

---

## 習得する概念

| 概念 | 関連ドキュメント |
|---|---|
| UE シェーダーシステム | [18-unreal-engine-shaders.md](../concepts/18-unreal-engine-shaders.md) |
| PBR・IBL | [08-pbr-theory.md](../concepts/08-pbr-theory.md) |
| Compute Shader | [15-compute-shaders.md](../concepts/15-compute-shaders.md) |
| レンダリングアーキテクチャ | [11-render-graph.md](11-render-graph.md) |

---

## フェーズ分け

### フェーズ 12-1: UE5 マテリアルシステムの深層理解

**実装項目**:
- `r.ShaderDevelopmentMode=1` で開発モード有効化
- マテリアルグラフのコンパイル結果 HLSL の確認（`SHADERSOURCE` 出力）
- `Material Stats` でシェーダー命令数を確認
- ShadingModel ごとの G-Buffer レイアウト理解（`SHADINGMODELID_DEFAULT_LIT` 等）
- Substrate マテリアルシステムへの移行（UE 5.2+）

```
UE5 G-Buffer レイアウト（GBuffer A〜E）:
  GBufferA: WorldNormal.xyz + PerObjectGBufferData
  GBufferB: Metallic + Specular + Roughness + ShadingModelID
  GBufferC: BaseColor.rgb + IndirectIrradiance
  GBufferD: CustomData（SSS係数, Clearcoat など Shading Model 依存）
  GBufferE: PrecomputedShadowFactors
```

---

### フェーズ 12-2: Custom Expression ノードの実用化

**実装項目**:
- 複雑な数式（ノイズ・カスタム BRDF・SDF）をインライン HLSL で実装
- `PixelMaterialInputs` 構造体の理解（マテリアル出力ピンへのアクセス）
- `MaterialFloat3` 型の受け渡し規則
- RenderDoc でコンパイル結果の実際の HLSL を確認

```hlsl
// Custom Expression でのノイズ生成（コードフィールドに貼り付け）
// Input0: UV (float2), Input1: time (float)
float2 p = Input0;
float t  = Input1;

float n = 0.0;
float a = 0.5;
for (int i = 0; i < 4; i++) {
    n += a * (sin(dot(p, float2(127.1, 311.7)) + t) * 43758.5);
    n  = frac(n);
    p *= 2.1;
    a *= 0.5;
}
return float3(n, n, n);
```

---

### フェーズ 12-3: .ush / .usf カスタムシェーダーライブラリ

**実装項目**:
- プラグイン構造のセットアップ（`MyPlugin/Shaders/Private/`）
- `Build.cs` でシェーダーパスを登録
- カスタム関数ライブラリ（`MyFunctions.ush`）を作成
- Custom Expression から `#include "/MyPlugin/MyFunctions.ush"` でインクルード

```
MyShaderPlugin/
├── MyShaderPlugin.uplugin
├── Source/
│   └── MyShaderPlugin/
│       ├── MyShaderPlugin.Build.cs   ← パス登録
│       └── Private/
│           └── MyGlobalShader.cpp
└── Shaders/
    ├── Private/
    │   └── MyGlobalShader.usf
    └── MyFunctions.ush               ← Custom Expression からインクルード可能
```

```csharp
// Build.cs でシェーダーパスを登録
public class MyShaderPlugin : ModuleRules {
    public MyShaderPlugin(ReadOnlyTargetRules Target) : base(Target) {
        string shadersDir = Path.Combine(ModuleDirectory, "../../Shaders");
        if (!Directory.Exists(shadersDir)) {
            string msg = string.Format("No Shaders directory found in {0}", shadersDir);
            throw new DirectoryNotFoundException(msg);
        }
        AddShaderSourceDirectoryMapping("/MyPlugin", shadersDir);
    }
}
```

---

### フェーズ 12-4: Global Shader（C++ 駆動のカスタム描画パス）

**実装項目**:
- `FGlobalShader` サブクラスの実装
- `IMPLEMENT_GLOBAL_SHADER` マクロで .usf と紐付け
- `SHADER_PARAMETER_STRUCT` でパラメータを型安全に定義
- `FRDGBuilder`（Render Dependency Graph）を使ったパス追加
- `SceneViewExtensions` でエンジンの描画パスに割り込み

```cpp
// カスタム Compute Shader の実装
class FMyComputeShader : public FGlobalShader {
    DECLARE_GLOBAL_SHADER(FMyComputeShader);
    SHADER_USE_PARAMETER_STRUCT(FMyComputeShader, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputTexture)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
        SHADER_PARAMETER_SAMPLER(SamplerState, InputSampler)
        SHADER_PARAMETER(FVector2f, InvOutputSize)
        SHADER_PARAMETER(float, Time)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) {
        return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
    }
};
IMPLEMENT_GLOBAL_SHADER(FMyComputeShader, "/MyPlugin/MyGlobalShader.usf", "MainCS", SF_Compute);

// SceneViewExtension での実行
void FMySceneViewExtension::PostRenderBasePassDeferred_RenderThread(
    FRDGBuilder& GraphBuilder,
    FSceneView& View,
    const FRenderTargetBindingSlots& BasePassTargets,
    TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures)
{
    FMyComputeShader::FParameters* PassParams =
        GraphBuilder.AllocParameters<FMyComputeShader::FParameters>();
    PassParams->OutputTexture = GraphBuilder.CreateUAV(myOutputTexture);
    PassParams->Time = View.Family->Time.GetWorldTimeSeconds();

    TShaderMapRef<FMyComputeShader> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
    FComputeShaderUtils::AddPass(GraphBuilder,
        RDG_EVENT_NAME("MyCustomPass"), Shader, PassParams,
        FIntVector(FMath::DivideAndRoundUp(View.ViewRect.Width(), 8),
                   FMath::DivideAndRoundUp(View.ViewRect.Height(), 8), 1));
}
```

---

### フェーズ 12-5: カスタム Shading Model

**実装項目**:
- `EShaderModelID` に新しい ID を追加
- `ToonModel` の例として簡易実装
- G-Buffer カスタムデータの割り当て
- `ShadingModelsMaterial.ush` の `GetShadingModelColor` などにエントリ追加

```hlsl
// MyToonShading.ush（エンジンソースを改変する方法と
//                   プラグインで差し込む方法を理解する）

#if SHADINGMODELID == SHADINGMODELID_MYTOON
    float4 CustomData = Material.GetMaterialCustomData0();
    float  ShadowSharpness = CustomData.r; // 影の硬さ
    float  ShadowBias      = CustomData.g;

    float NdotL = dot(N, L);
    float toon  = smoothstep(ShadowBias - ShadowSharpness,
                             ShadowBias + ShadowSharpness, NdotL);
    DiffuseColor = GBuffer.BaseColor * toon;
#endif
```

---

### フェーズ 12-6: Vertex Factory（カスタム頂点フォーマット）

**実装項目**:
- `FVertexFactory` サブクラスの最小実装
- `FVertexFactoryInput` と `FVertexFactoryIntermediates` の定義
- `VertexFactoryGetWorldPosition` などのインターフェース関数の実装
- GPU 手続き生成メッシュへの応用（SDF から動的頂点生成）

```cpp
// 最小 Vertex Factory 実装
IMPLEMENT_VERTEX_FACTORY_TYPE(
    FMyVertexFactory,           // C++ クラス
    "/MyPlugin/MyVertexFactory.ush", // .ush ファイル
    EVertexFactoryFlags::UsedWithMaterials |
    EVertexFactoryFlags::SupportsDynamicLighting
);
```

---

### フェーズ 12-7: Niagara GPU シミュレーション

**実装項目**:
- Niagara GPU Simulation Stage の作成
- カスタム HLSL スクリプト（Niagara 専用 HLSL スタイル）
- データインターフェース（Custom Data Interface の実装）
- CPU → GPU パーティクルのシームレスな移行

```hlsl
// Niagara GPU Simulation スクリプト（カスタム HLSL）
void SimulateParticle(inout NiagaraParticle Particle) {
    // Curl Noise で移動
    float3 pos  = Particle.Position;
    float3 curl = CurlNoise(pos * 0.3, Engine_Time);
    Particle.Velocity  += curl * 10.0 * Engine_DeltaTime;
    Particle.Position  += Particle.Velocity * Engine_DeltaTime;
    Particle.Age       += Engine_DeltaTime;

    if (Particle.Age > Particle.Lifetime)
        Particle.IsValid = false;
}
```

---

### フェーズ 12-8: Lumen / Nanite / TSR との正しい連携

**実装項目**:
- Nanite 非対応素材の特定と代替（WPO・Translucent）
- Lumen に正しく参加するシェーダーの条件確認
- TSR 用の Velocity 出力（WPO を使うマテリアルでの対処）
- `r.Lumen.Reflections.Allow` 等のコンソール変数理解

```
Nanite 対応チェックリスト:
  ✅ Opaque / Masked マテリアル
  ✅ Pixel Depth Offset なし
  ✅ UE 5.3+ での WPO（限定的に対応）
  ❌ Translucent
  ❌ Tessellation（UE5 では廃止傾向）
  ❌ 2 パス描画が必要なマテリアル
```

---

### フェーズ 12-9: Path Tracer との統合確認

**実装項目**:
- カスタムシェーダーが Path Tracer でも正しく評価されるか確認
- `#if PATH_TRACER` 分岐の適切な実装
- Material Emissive の物理的なスケール（cd/m²）への対応

---

## UE5 シェーダーデバッグツール

```
1. RenderDoc + UE プラグイン
   → フレームキャプチャ、シェーダーデバッグ

2. ビューモード
   → Shader Complexity（赤: 重い）
   → Nanite Triangles / Overdraw
   → Lighting Only / Base Color Only

3. コンソールコマンド
   r.ShaderDevelopmentMode=1    ← エラー詳細表示
   stat shaderbatch             ← シェーダー統計
   stat gpu                     ← GPU タイム
   r.DumpShaderDebugInfo=1      ← コンパイル済み HLSL を出力

4. Unreal Insights
   → フレームグラフ、GPU タイムライン（PIX 相当）
```

---

## ファイル構成（プラグイン）

```
MyShaderPlugin/
├── MyShaderPlugin.uplugin
├── Content/
│   └── M_Custom.uasset         ← カスタムマテリアルアセット
├── Source/
│   └── MyShaderPlugin/
│       ├── MyShaderPlugin.Build.cs
│       ├── Public/
│       │   ├── MySceneViewExtension.h
│       │   └── MyVertexFactory.h
│       └── Private/
│           ├── MyShaderPlugin.cpp   ← モジュール登録
│           ├── MySceneViewExtension.cpp
│           ├── MyGlobalShader.cpp
│           └── MyVertexFactory.cpp
└── Shaders/
    ├── MyFunctions.ush          ← Custom Expression 用
    ├── Private/
    │   ├── MyGlobalShader.usf
    │   └── MyVertexFactory.ush
    └── MyToonShading.ush
```

---

## 確認チェックリスト

- [ ] Custom Expression のカスタム関数が正しく HLSL に埋め込まれる
- [ ] Global Shader が UE5 の RDG に正しく登録され、フレームキャプチャで確認できる
- [ ] カスタム Shading Model が G-Buffer に正しく書き込まれる
- [ ] Nanite 対応チェックリストを全て確認した
- [ ] Niagara GPU スクリプトで 100 万パーティクルが実行される
- [ ] Path Tracer でカスタムマテリアルが正常にレンダリングされる

---

## 関連ドキュメント
- [11-render-graph.md](11-render-graph.md) - 前フェーズ
- [13-slang-rewrite.md](13-slang-rewrite.md) - 次フェーズ
- [../concepts/18-unreal-engine-shaders.md](../concepts/18-unreal-engine-shaders.md)
