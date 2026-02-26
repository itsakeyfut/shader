# 10 - Shadows（影）

## Shadow Mapping 基礎

### アルゴリズム概要
1. **深度マップ生成**: 光源視点からシーンを描画し、深度を記録
2. **シャドウテスト**: 通常描画時に各ピクセルを光源空間に変換し、深度マップと比較

### パス 1: 深度マップ生成
```hlsl
// Shadow Map 生成用 VS
float4 ShadowVS(float3 pos : POSITION) : SV_Position {
    return mul(lightViewProj, float4(pos, 1.0));
    // PS は不要（深度のみ出力）または最小 PS
}
```

```cpp
// D3D11 セットアップ
D3D11_TEXTURE2D_DESC shadowDesc = {};
shadowDesc.Width     = SHADOW_MAP_SIZE; // 例: 2048
shadowDesc.Height    = SHADOW_MAP_SIZE;
shadowDesc.Format    = DXGI_FORMAT_R32_TYPELESS; // または D32_FLOAT
shadowDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
```

### パス 2: シャドウテスト
```hlsl
cbuffer ShadowData : register(b1) {
    float4x4 lightViewProj;
    float2   shadowMapSize;    // 例: (2048, 2048)
    float    shadowBias;       // 例: 0.005
};

SamplerComparisonState shadowSampler : register(s1); // COMPARISON サンプラー

float SampleShadow(Texture2D shadowMap, float3 worldPos) {
    // ライト空間に変換
    float4 lightSpacePos = mul(lightViewProj, float4(worldPos, 1.0));

    // 透視除算 → NDC
    float3 projCoords = lightSpacePos.xyz / lightSpacePos.w;

    // D3D NDC [−1,1] → UV [0,1]
    float2 shadowUV = projCoords.xy * float2(0.5, -0.5) + 0.5;

    // 深度比較
    float currentDepth = projCoords.z - shadowBias;
    return shadowMap.SampleCmpLevelZero(shadowSampler, shadowUV, currentDepth);
    // 0.0 = 影、1.0 = 照らされている
}
```

---

## シャドウアクネ・ピーターパニング

### シャドウアクネ（Self-Shadowing Artifact）
```
原因: 深度精度の限界により、サーフェスが自分自身を遮蔽してしまう
見た目: ストライプ状のセルフシャドウ

対策: バイアス（オフセット）を加える
```

```hlsl
// 定数バイアス（簡単だが調整が難しい）
float currentDepth = projCoords.z - 0.005;

// 法線バイアス（サーフェスの傾きに応じて調整）
float cosTheta = max(dot(N, L), 0.0);
float bias = max(0.05 * (1.0 - cosTheta), 0.005);
float currentDepth = projCoords.z - bias;
```

### ピーターパニング（Peter-Panning）
```
原因: バイアスが大きすぎて影がオブジェクトから浮いて見える

対策:
  1. バイアスを小さくする（アクネとのトレードオフ）
  2. 深度クランプ (RSSetState: DepthClipEnable = FALSE)
  3. フロントフェースカリング（Shadow Pass で裏面のみ描画）
```

```cpp
// フロントフェースカリング（Shadow Pass 用）
D3D11_RASTERIZER_DESC shadowRSDesc = {};
shadowRSDesc.CullMode              = D3D11_CULL_FRONT; // 通常は BACK
shadowRSDesc.FillMode              = D3D11_FILL_SOLID;
shadowRSDesc.DepthClipEnable       = FALSE; // 深度クランプ
```

---

## PCF（Percentage Closer Filtering）

複数の深度サンプルを平均化して軟らかい影の境界を得る。

```hlsl
float PCF(Texture2D shadowMap, float3 worldPos, int kernelSize) {
    float4 lightPos = mul(lightViewProj, float4(worldPos, 1.0));
    float3 proj     = lightPos.xyz / lightPos.w;
    float2 uv       = proj.xy * float2(0.5, -0.5) + 0.5;
    float  depth    = proj.z - shadowBias;

    float shadow     = 0.0;
    float texelSize  = 1.0 / 2048.0;
    int   halfKernel = kernelSize / 2;

    for (int x = -halfKernel; x <= halfKernel; x++) {
        for (int y = -halfKernel; y <= halfKernel; y++) {
            float2 offset = float2(x, y) * texelSize;
            shadow += shadowMap.SampleCmpLevelZero(shadowSampler, uv + offset, depth);
        }
    }
    return shadow / float(kernelSize * kernelSize);
}
```

**Poisson ディスクサンプリング**でより少ないサンプルで同等品質を得られる:
```hlsl
static const float2 poissonDisk[16] = {
    float2(-0.94201624, -0.39906216),
    float2( 0.94558609, -0.76890725),
    // ... 16サンプルのポアソンディスク
};

for (int i = 0; i < 16; i++) {
    shadow += shadowMap.SampleCmpLevelZero(
        shadowSampler, uv + poissonDisk[i] / 700.0, depth);
}
shadow /= 16.0;
```

---

## PCSS（Percentage Closer Soft Shadows）

光源サイズに基づいて影の柔らかさを動的に変化させる（接触部分は鋭く、遠い部分は柔らかく）。

```hlsl
// ステップ 1: Blocker 探索（平均遮蔽深度）
float FindBlockerDistance(Texture2D shadowMap, float2 uv, float currentDepth, float searchWidth) {
    float blockerSum   = 0.0;
    int   blockerCount = 0;

    for (int i = 0; i < NUM_BLOCKER_SAMPLES; i++) {
        float2 offset = poissonDisk[i] * searchWidth;
        float  shadowDepth = shadowMap.Sample(pointSampler, uv + offset).r;
        if (shadowDepth < currentDepth) {
            blockerSum += shadowDepth;
            blockerCount++;
        }
    }
    return (blockerCount > 0) ? blockerSum / blockerCount : -1.0;
}

// ステップ 2: Penumbra 幅の計算
float PenumbraWidth(float blockerDist, float receiverDist) {
    return (receiverDist - blockerDist) * LIGHT_SIZE / blockerDist;
}

// ステップ 3: PCF with dynamic kernel size
float PCSS(Texture2D shadowMap, float3 worldPos) {
    // ... 上記の組み合わせ
    float avgBlocker = FindBlockerDistance(shadowMap, uv, depth, BLOCKER_SEARCH_WIDTH);
    if (avgBlocker < 0.0) return 1.0; // 影なし

    float penumbra = PenumbraWidth(avgBlocker, depth);
    return PCF(shadowMap, uv, depth, penumbra);
}
```

---

## VSM（Variance Shadow Maps）

統計的手法（チェビシェフの不等式）を使い、PCF なしでソフトシャドウを生成。

```hlsl
// Shadow Map に (depth, depth²) を格納
struct ShadowOut {
    float2 moments : SV_Target; // RG32_FLOAT フォーマット
};

ShadowOut ShadowPS(float4 pos : SV_Position) {
    ShadowOut output;
    float d = pos.z;
    output.moments = float2(d, d * d);
    return output;
}

// 比較時: チェビシェフの不等式
float ChebyshevUpperBound(float2 moments, float t) {
    float p  = (t <= moments.x) ? 1.0 : 0.0;
    float variance = moments.y - moments.x * moments.x;
    variance = max(variance, 0.00002); // 最小分散

    float d    = t - moments.x;
    float pMax = variance / (variance + d * d); // チェビシェフ

    return max(p, pMax);
}

float VSM(Texture2D shadowMap, float2 uv, float depth) {
    float2 moments = shadowMap.Sample(linearSampler, uv).rg; // バイリニアOK
    return ChebyshevUpperBound(moments, depth);
}
```

**利点**: 深度マップをバイリニアフィルタリング可能 → ぼかしが安価
**欠点**: Light-Bleeding（明るいオブジェクトの近くに偽の影）

---

## CSM（Cascaded Shadow Maps）

視錐台を複数のカスケードに分割し、各カスケードに個別のシャドウマップを使用。
近くは高解像度、遠くは低解像度。

```cpp
// フラスタム分割（対数分割が実用的）
float lambda = 0.75; // 線形と対数の混合
for (int i = 0; i < NUM_CASCADES; i++) {
    float p = (i + 1) / float(NUM_CASCADES);
    float log  = nearZ * pow(farZ / nearZ, p);
    float uni  = nearZ + (farZ - nearZ) * p;
    cascadeSplits[i] = lerp(uni, log, lambda);
}
```

```hlsl
// PS でカスケード選択
int GetCascadeIndex(float viewDepth) {
    for (int i = 0; i < NUM_CASCADES; i++) {
        if (viewDepth < cascadeSplits[i]) return i;
    }
    return NUM_CASCADES - 1;
}

float SampleCSM(Texture2DArray shadowMaps, float3 worldPos, float viewDepth) {
    int   cascade   = GetCascadeIndex(viewDepth);
    float4x4 lvp    = lightViewProjs[cascade];

    float4 lightPos = mul(lvp, float4(worldPos, 1.0));
    float3 proj     = lightPos.xyz / lightPos.w;
    float2 uv       = proj.xy * float2(0.5, -0.5) + 0.5;

    return shadowMaps.SampleCmpLevelZero(shadowSampler,
        float3(uv, (float)cascade), proj.z - bias);
}
```

**カスケード間のブレンド**: 境界で突然切り替わるポッピングを避けるため、
隣接カスケード間を補間するブレンドゾーンを設ける。

---

## Point Light Shadow（Omnidirectional Shadow Map）

点光源は全方向に光を照射するため、Cube Shadow Map を使用。

```cpp
// 6面を描画（各面でカメラを 90° 回転）
float3 targets[6] = { {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1} };
float3 ups[6]     = { {0,1,0},{0,1,0},{0,0,-1},{0,0,1},{0,1,0},{0,1,0} };

for (int face = 0; face < 6; face++) {
    float4x4 view = LookAt(lightPos, lightPos + targets[face], ups[face]);
    // 各面を 90° FOV で描画
}
```

```hlsl
// 比較（TextureCube を使用）
TextureCube shadowCube : register(t4);

float SampleOmniShadow(float3 worldPos, float3 lightPos) {
    float3 toLight = worldPos - lightPos;
    float  dist    = length(toLight);
    float3 dir     = toLight / dist;

    // テクセルからの深度（0〜1 に正規化）
    float storedDepth = shadowCube.Sample(pointSampler, dir).r;
    float currentDepth = (dist - nearZ) / (farZ - nearZ);

    return (currentDepth - bias < storedDepth) ? 1.0 : 0.0;
}
```

---

## Ray Traced Shadow（DXR、発展）

- `TraceRay()` で光源へのレイを飛ばし、遮蔽を確認
- ソフトシャドウ: 光源面積内のランダム点に複数レイ
- 参照: [14-advanced-rendering.md](14-advanced-rendering.md)、DirectX Raytracing

---

## 関連ドキュメント
- [02-coordinate-spaces.md](02-coordinate-spaces.md) - ライト空間変換
- [06-lighting-models.md](06-lighting-models.md) - ライティングとの統合
- [../roadmap/02-lighting.md](../roadmap/02-lighting.md) - Shadow Map 実装フェーズ
