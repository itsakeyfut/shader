# Roadmap: Phase 2 - Lighting

**プロジェクト**: `D:/dev/shader/lighting/`
**API**: Direct3D 11
**目標**: 現実的な陰影を持つ 3D シーンを実装する

---

## 習得する概念

| 概念 | 関連ドキュメント |
|---|---|
| 法線変換（法線行列・TBN） | [04-normal-handling.md](../concepts/04-normal-handling.md) |
| Lambert / Blinn-Phong | [06-lighting-models.md](../concepts/06-lighting-models.md) |
| Shadow Mapping | [10-shadows.md](../concepts/10-shadows.md) |
| SSAO | [13-ambient-occlusion.md](../concepts/13-ambient-occlusion.md) |
| テクスチャサンプリング | [05-texturing-sampling.md](../concepts/05-texturing-sampling.md) |

---

## フェーズ分け

### フェーズ 2-1: Blinn-Phong ライティング（1点光源）

**実装項目**:
- 法線をワールド空間に変換（法線行列）
- Diffuse（Lambert）+ Specular（Blinn-Phong）計算
- Ambient + Diffuse + Specular の合算

```hlsl
cbuffer LightData : register(b1) {
    float3 lightPos;
    float  lightRadius;
    float3 lightColor;
    float  lightIntensity;
    float3 cameraPos;
    float  pad;
};

float4 PSMain(VSOutput input) : SV_Target {
    float3 N = normalize(input.worldNormal);
    float3 L = normalize(lightPos - input.worldPos);
    float3 V = normalize(cameraPos - input.worldPos);
    float3 H = normalize(L + V);

    float3 albedo   = albedoTex.Sample(samp, input.uv).rgb;
    float  NdotL    = max(dot(N, L), 0.0);
    float  NdotH    = max(dot(N, H), 0.0);
    float  dist     = length(lightPos - input.worldPos);
    float  atten    = clamp(1.0 - dist / lightRadius, 0.0, 1.0) * lightIntensity;
    float3 radiance = lightColor * atten;

    float3 diffuse  = albedo * NdotL;
    float3 specular = pow(NdotH, shininess) * specularColor;
    float3 ambient  = float3(0.03, 0.03, 0.03) * albedo;

    return float4(ambient + (diffuse + specular) * radiance, 1.0);
}
```

**確認方法**: スフィアやキューブに滑らかなシェーディングが入る

---

### フェーズ 2-2: 複数光源（Directional / Point / Spot）

**実装項目**:
- 光源種別を定数バッファで管理
- 各種光源の計算関数を実装
- 複数光源のループ処理

```hlsl
struct Light {
    float3 position;
    uint   type;        // 0=Directional, 1=Point, 2=Spot
    float3 direction;
    float  range;
    float3 color;
    float  intensity;
    float  innerAngle;
    float  outerAngle;
    float2 pad;
};

cbuffer LightBuffer : register(b1) {
    Light lights[MAX_LIGHTS];
    uint  numLights;
};
```

---

### フェーズ 2-3: ノーマルマッピング（タンジェント空間 TBN）

**実装項目**:
- 頂点データにタンジェントを追加
- CPU 側でのタンジェント計算（MikkTSpace 準拠）
- VS での TBN 行列構築
- PS でのノーマルマップサンプリング + TBN 変換

```hlsl
// VS
VSOutput VSMain(VSInput input) {
    float3 T = normalize(mul((float3x3)modelMatrix, input.tangent.xyz));
    float3 N = normalize(mul((float3x3)normalMatrix, input.normal));
    T = normalize(T - dot(T, N) * N); // Gram-Schmidt
    float3 B = cross(N, T) * input.tangent.w;

    output.tangent   = T;
    output.bitangent = B;
    output.normal    = N;
}

// PS
float3 tangentNormal = normalMap.Sample(samp, uv).rgb * 2.0 - 1.0;
float3x3 TBN = float3x3(input.tangent, input.bitangent, input.normal);
float3 N = normalize(mul(tangentNormal, TBN));
```

**確認方法**: 表面に細かい凹凸の見た目が追加される

---

### フェーズ 2-4: Shadow Mapping（指向性ライト）

**実装項目**:
- 深度テクスチャ + DSV の作成
- Shadow Pass（光源視点から深度のみ描画）
- ライト空間変換の CB 追加
- Shadow テスト（`SampleCmpLevelZero`）
- バイアスでアクネを抑制

```cpp
// 深度テクスチャ作成
D3D11_TEXTURE2D_DESC shadowDesc = {};
shadowDesc.Width              = 2048;
shadowDesc.Height             = 2048;
shadowDesc.Format             = DXGI_FORMAT_R32_TYPELESS;
shadowDesc.BindFlags          = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
shadowDesc.SampleDesc.Count   = 1;

// SRV: R32_FLOAT として読み取り
D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
srvDesc.Format                    = DXGI_FORMAT_R32_FLOAT;
srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
srvDesc.Texture2D.MipLevels       = 1;
```

**確認方法**: 地面や壁に影が落ちる（セルフシャドウアクネとの戦い）

---

### フェーズ 2-5: PCF でシャドウ品質改善

**実装項目**:
- `SamplerComparisonState` と `SampleCmpLevelZero`
- 3×3 または 5×5 PCF カーネル
- Poisson ディスクサンプリング（オプション）

```hlsl
float PCFShadow(float2 shadowUV, float currentDepth) {
    float shadow    = 0.0;
    float texelSize = 1.0 / 2048.0;
    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            shadow += shadowMap.SampleCmpLevelZero(
                shadowSampler,
                shadowUV + float2(x, y) * texelSize,
                currentDepth - 0.005
            );
        }
    }
    return shadow / 9.0;
}
```

---

### フェーズ 2-6: CSM（複数カスケード）

**実装項目**:
- フラスタム分割（対数分割）
- カスケードごとのライトビュープロジェクション行列
- `Texture2DArray` でカスケードシャドウマップ
- PS でのカスケード選択とブレンド

---

### フェーズ 2-7: SSAO

**実装項目**:
- G-Buffer に法線・深度を書き込む（MRT）
- ランダムサンプルカーネル（CPU 生成）
- ノイズテクスチャ（4×4 タイリング）
- SSAO Compute Pass（半球サンプリング・深度比較）
- バイラテラルブラー

---

### フェーズ 2-8（発展）: Deferred Shading の基礎 G-Buffer

**実装項目**:
- MRT セットアップ（Albedo・Normal・Depth）
- Geometry Pass と Lighting Pass の分離
- フルスクリーンクワッドでのライティング計算

---

## ファイル構成（完成時）

```
lighting/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── D3DApp.cpp/.h
│   ├── Scene.cpp/.h       ← メッシュ・マテリアル管理
│   ├── LightSystem.cpp/.h ← 光源の管理・CB 更新
│   ├── ShadowMap.cpp/.h   ← シャドウパス管理
│   └── PostProcess.cpp/.h ← SSAO
└── shaders/
    ├── common.hlsli
    ├── lighting.hlsli     ← 光源計算共通関数
    ├── geometry_vs.hlsl
    ├── geometry_ps.hlsl
    ├── shadow_vs.hlsl
    ├── ssao_ps.hlsl
    └── blur_ps.hlsl
```

---

## 確認チェックリスト

- [ ] 法線行列を正しく適用した（非一様スケール時に正確）
- [ ] TBN の w 成分（ミラー UV）を正しく処理した
- [ ] Shadow Map のバイアスをチューニングした（アクネ・ピーターパン解消）
- [ ] PCF のカーネルサイズとコストのバランスを理解した
- [ ] SSAO のサンプル数とノイズ・ブラーのトレードオフを理解した

---

## 関連ドキュメント
- [01-hello-triangle.md](01-hello-triangle.md) - 前フェーズ
- [../concepts/06-lighting-models.md](../concepts/06-lighting-models.md)
- [../concepts/10-shadows.md](../concepts/10-shadows.md)
- [03-pbr.md](03-pbr.md) - 次フェーズ
