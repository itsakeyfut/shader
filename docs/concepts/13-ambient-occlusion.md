# 13 - Ambient Occlusion（環境光遮蔽）

## SSAO 基礎

Screen Space Ambient Occlusion。深度バッファから局所的な環境光の遮蔽を近似。

### アルゴリズム概要
1. G-Buffer から法線・深度を読む
2. 各ピクセルの周囲に半球サンプリング
3. サンプルが深度バッファ内の表面より後ろにあれば遮蔽
4. 遮蔽率を AO 値として出力
5. ブラーでノイズを除去

### 実装

```hlsl
cbuffer SSAOData : register(b0) {
    float4x4 projection;
    float4x4 invProjection;
    float    radius;       // サンプル半径（ビュー空間）
    float    bias;         // 自己遮蔽防止バイアス
    int      numSamples;   // サンプル数（例: 64）
};

// サンプルカーネル（半球上のランダム点）
// CPU 側で事前生成して cbuffer に渡す
float3 ssaoKernel[64]; // 半球内のランダムサンプル

// ランダムノイズテクスチャ（4×4 タイリング）
Texture2D<float2> noiseTex : register(t2); // RG にランダム回転ベクトル

float SSAOMain(float2 uv : TEXCOORD0) : SV_Target {
    // G-Buffer から取得
    float  depth      = depthBuffer.Sample(pointSampler, uv).r;
    float3 viewPos    = ReconstructViewPos(uv, depth, invProjection);
    float3 viewNormal = normalBuffer.Sample(pointSampler, uv).xyz * 2.0 - 1.0;
    viewNormal        = normalize(viewNormal);

    // ノイズテクスチャで TBN をランダム回転
    float2 noiseUV  = uv * screenSize / 4.0; // 4×4 タイリング
    float3 randVec  = float3(noiseTex.Sample(wrapSampler, noiseUV), 0.0);

    // Gram-Schmidt で接線空間を構築
    float3 tangent   = normalize(randVec - viewNormal * dot(randVec, viewNormal));
    float3 bitangent = cross(viewNormal, tangent);
    float3x3 TBN     = float3x3(tangent, bitangent, viewNormal);

    float occlusion = 0.0;
    [loop]
    for (int i = 0; i < numSamples; i++) {
        // サンプルをタンジェント→ビュー空間に変換
        float3 samplePos = mul(ssaoKernel[i], TBN);
        samplePos        = viewPos + samplePos * radius;

        // サンプルのスクリーン座標を計算
        float4 offset = mul(projection, float4(samplePos, 1.0));
        offset.xyz   /= offset.w;
        float2 sampleUV = offset.xy * float2(0.5, -0.5) + 0.5;

        // 深度比較
        float sampleDepth = depthBuffer.Sample(pointSampler, sampleUV).r;
        float3 sampleView = ReconstructViewPos(sampleUV, sampleDepth, invProjection);

        // 遮蔽判定（スムーズな境界で距離依存）
        float rangeCheck  = smoothstep(0.0, 1.0, radius / abs(viewPos.z - sampleView.z));
        occlusion += (sampleView.z <= samplePos.z - bias ? 1.0 : 0.0) * rangeCheck;
    }

    return 1.0 - (occlusion / float(numSamples));
}
```

### サンプルカーネル生成（CPU 側）
```cpp
// 半球内のランダムサンプル（中心寄りに集中）
for (int i = 0; i < 64; i++) {
    XMFLOAT3 sample(
        RandomFloat(-1, 1), RandomFloat(-1, 1), RandomFloat(0, 1));
    sample = Normalize(sample);
    sample *= RandomFloat(0, 1);
    float scale = i / 64.0f;
    scale = Lerp(0.1f, 1.0f, scale * scale); // 中心寄りに集中
    sample *= scale;
    kernel.push_back(sample);
}
```

### ブラーパス（深度を考慮したブラー）
```hlsl
float BlurSSAO(float2 uv : TEXCOORD0) : SV_Target {
    float sum = 0.0;
    float count = 0.0;
    float centerDepth = depthBuffer.Sample(pointSampler, uv).r;

    for (int x = -2; x <= 2; x++) {
        for (int y = -2; y <= 2; y++) {
            float2 offset = float2(x, y) / screenSize;
            float  sDepth = depthBuffer.Sample(pointSampler, uv + offset).r;

            // 深度差が大きいサンプルは無視（エッジを保持）
            if (abs(sDepth - centerDepth) < 0.05) {
                sum += ssaoTex.Sample(pointSampler, uv + offset).r;
                count++;
            }
        }
    }
    return sum / count;
}
```

---

## HBAO（Horizon-Based AO）

NVIDIA が開発。各方向の「地平線角度」を計算して遮蔽を求める。

```
各ピクセルで複数の方向に Ray March（スクリーン空間）
各方向で「地平線」（最大仰角）を求める
地平線以下が遮蔽されている

AO = 1 - (平均地平線角度 / π/2)
```

### SSAO との比較
| 特性 | SSAO | HBAO |
|---|---|---|
| 品質 | 中 | 高（接触部分がよりリアル）|
| コスト | 低〜中 | 中〜高 |
| アーティファクト | 球状のハロー | 方向依存のストライプ |
| 法線依存 | あり | あり（より重要） |

---

## GTAO（Ground Truth AO）

Activision が提案（Jorge Jimenez, 2016）。
より物理的に正確な積分近似で、HBAO の進化版。

```
各方向で2つの地平線角度（前後）を積分
Multibounce 近似（1回バウンスの AO）
ベイクド AO との組み合わせで精度向上

実装: Intel XeGTAO が OpenSource で利用可能
```

---

## RTAO（Ray Traced AO）

DXR を使ったリアルタイム RTAO。最も正確だがコストが高い。

```hlsl
// RTAO Ray Generation Shader
[shader("raygeneration")]
void RaytracingAOMain() {
    uint2 pixelPos  = DispatchRaysIndex().xy;
    float3 worldPos = GetWorldPos(pixelPos);
    float3 N        = GetNormal(pixelPos);

    float occlusion = 0.0;
    for (int i = 0; i < NUM_AO_RAYS; i++) {
        float3 dir    = CosineSampleHemisphere(N, RandomFloat2(seed));
        RayDesc ray;
        ray.Origin    = worldPos + N * 0.01;
        ray.Direction = dir;
        ray.TMin      = 0.001;
        ray.TMax      = AO_RADIUS;

        AORayPayload payload = { 1.0 };
        TraceRay(sceneBVH, RAY_FLAG_CULL_BACK_FACING_TRIANGLES,
                 0xFF, 0, 0, 0, ray, payload);
        occlusion += payload.hitDist < AO_RADIUS ? 1.0 : 0.0;
    }

    aoOutput[pixelPos] = 1.0 - occlusion / NUM_AO_RAYS;
}
```

---

## ベイクド AO vs ダイナミック AO

| 種別 | 精度 | コスト | 動的オブジェクト |
|---|---|---|---|
| ベイクド AO | 最高（ライトマップ） | 事前計算 | 非対応 |
| SSAO | 低〜中 | フレームごと | 対応 |
| HBAO | 中〜高 | フレームごと | 対応 |
| GTAO | 高 | フレームごと | 対応 |
| RTAO | 最高 | フレームごと | 対応 |

### 実用的な組み合わせ
```
静的オブジェクト: ベイクド AO（ライトマップ）
動的オブジェクト: SSAO/HBAO
高品質シーン:    ベイクド + SSAO（乗算合成）

color *= lerp(1.0, ssaoValue, ssaoStrength);
color *= lerp(1.0, bakedAO, bakedAOStrength);
```

---

## 関連ドキュメント
- [11-post-processing.md](11-post-processing.md) - ポストプロセスチェーンでの配置
- [14-advanced-rendering.md](14-advanced-rendering.md) - Deferred Shading の G-Buffer
- [../roadmap/02-lighting.md](../roadmap/02-lighting.md) - SSAO 実装フェーズ
