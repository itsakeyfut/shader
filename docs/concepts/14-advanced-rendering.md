# 14 - Advanced Rendering（高度なレンダリング）

## Deferred Shading（遅延シェーディング）

ライティング計算をジオメトリパスから分離し、多数の光源を効率的に処理する。

### G-Buffer 設計

```
Geometry Pass 出力:
  RT0 (RGBA8):  Albedo(RGB) + Metalness(A)
  RT1 (RGBA16F): WorldNormal(RGB) + Roughness(A)
  RT2 (RG16F):  Motion Vector
  DS  (D24S8):  Depth + Stencil

または:
  RT0 (RGBA8):  Albedo(RGB) + AO(A)
  RT1 (RG16F):  Normal (Oct-encoded 2ch)
  RT2 (RGBA8):  Metalness(R) + Roughness(G) + Emissive(B) + Flags(A)
```

### Geometry Pass（G-Buffer 書き込み）
```hlsl
struct GBufferOut {
    float4 albedoMetal  : SV_Target0;
    float4 normalRough  : SV_Target1;
    float2 motionVec    : SV_Target2;
};

GBufferOut GeometryPS(VSOutput input) {
    GBufferOut gbuf;
    float3 albedo    = albedoTex.Sample(samp, input.uv).rgb;
    float  metalness = pbrTex.Sample(samp, input.uv).r;
    float  roughness = pbrTex.Sample(samp, input.uv).g;
    float3 N         = SampleNormal(normalTex, samp, input.uv, input.TBN);

    gbuf.albedoMetal = float4(albedo, metalness);
    gbuf.normalRough = float4(N * 0.5 + 0.5, roughness);
    gbuf.motionVec   = CalcMotionVector(input.currentPos, input.prevPos);

    return gbuf;
}
```

### Lighting Pass（G-Buffer から読み取り）
```hlsl
float4 LightingPS(float2 uv : TEXCOORD0) : SV_Target {
    // G-Buffer デコード
    float4 albedoMetal = gbuf0.Sample(samp, uv);
    float4 normalRough = gbuf1.Sample(samp, uv);
    float  depth       = depthBuffer.Sample(samp, uv).r;

    float3 albedo    = albedoMetal.rgb;
    float  metalness = albedoMetal.a;
    float3 N         = normalRough.rgb * 2.0 - 1.0;
    float  roughness = normalRough.a;

    float3 worldPos = ReconstructWorldPos(uv, depth, invViewProj);
    float3 V        = normalize(cameraPos - worldPos);

    // 全光源の累積
    float3 Lo = float3(0, 0, 0);
    [loop]
    for (int i = 0; i < numLights; i++) {
        Lo += CookTorranceBRDF(N, V, lights[i], worldPos, albedo, metalness, roughness);
    }

    return float4(Lo, 1.0);
}
```

### Deferred の利点・欠点

| 特性 | 内容 |
|---|---|
| 光源数 | O(pixels × lights) → O(pixels + lights) |
| Overdraw | Lighting Pass では Overdraw なし |
| 透明度 | Forward で別途処理が必要 |
| MSAA | 複雑（Per-Sample Shading が必要） |
| メモリ | G-Buffer は帯域幅を消費 |

---

## Forward+（Tiled Forward Shading）

Forward レンダリングを維持しつつ、多数の光源をタイルベースカリングで効率化。

### Tiled Light Culling（Compute Shader）
```hlsl
// タイル単位でライトリストを構築
[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void BuildLightListCS(uint3 groupID : SV_GroupID, uint3 threadID : SV_GroupThreadID) {
    // タイルの深度範囲を計算
    // 視錐台タイルのAABBを生成
    // 各ライトとタイルのAABBを比較 → 交差するライトをリストに追加
}

// Forward Pass でタイルのライトリストを参照
float4 ForwardPlusPS(VSOutput input) : SV_Target {
    uint2  tileIndex  = (uint2)input.position.xy / TILE_SIZE;
    uint   tileOffset = tileIndex.y * numTilesX + tileIndex.x;
    uint   lightCount = lightGrid[tileOffset * 2 + 0];
    uint   lightStart = lightGrid[tileOffset * 2 + 1];

    float3 Lo = float3(0, 0, 0);
    for (uint i = 0; i < lightCount; i++) {
        uint lightIdx = lightIndexList[lightStart + i];
        Lo += EvaluateLight(input, lights[lightIdx]);
    }
    return float4(Lo, 1.0);
}
```

### Clustered Shading（3D タイル分割）
- Forward+ の発展版
- 画面を XY のタイルだけでなく Z（深度）でも分割
- カメラ近くの密集ライトも効率的に処理

---

## Screen Space Reflections（SSR）

スクリーン空間でのレイマーチングによる動的反射。

```hlsl
float3 SSR(float3 worldPos, float3 N, float3 V, float roughness) {
    // 粗い素材はSSRを使わない（IBLフォールバック）
    if (roughness > 0.4) return float3(0, 0, 0);

    float3 R = reflect(-V, N);

    // ビュー空間でレイマーチング
    float3 viewPos = mul(viewMatrix, float4(worldPos, 1.0)).xyz;
    float3 viewR   = normalize(mul((float3x3)viewMatrix, R));

    float3 hitPos = viewPos;
    float  stepSize = 0.1;

    for (int i = 0; i < MAX_STEPS; i++) {
        hitPos += viewR * stepSize;

        // スクリーン座標に投影
        float4 proj   = mul(projMatrix, float4(hitPos, 1.0));
        float2 hitUV  = (proj.xy / proj.w) * float2(0.5, -0.5) + 0.5;

        if (any(hitUV < 0.0) || any(hitUV > 1.0)) break;

        // 深度比較
        float sceneDepth = LinearizeDepth(depthBuffer.Sample(samp, hitUV).r);
        float rayDepth   = -hitPos.z;

        if (rayDepth > sceneDepth + 0.05) {
            // 二分探索で精密化
            return colorBuffer.Sample(samp, hitUV).rgb;
        }
        stepSize *= 1.05; // 距離に応じてステップを大きく
    }

    return float3(0, 0, 0); // ヒットなし → IBLにフォールバック
}
```

---

## Volumetric Fog / God Rays

```hlsl
// フォグの散乱係数
float HenyeyGreenstein(float cosTheta, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * PI * pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5));
}

// ボリュームライティング（Raymarching）
float3 VolumetricScattering(float3 rayOrigin, float3 rayDir, float rayLength) {
    float3 scattered = float3(0, 0, 0);
    float  stepSize  = rayLength / NUM_STEPS;
    float3 pos       = rayOrigin;

    for (int i = 0; i < NUM_STEPS; i++) {
        // ライトへの可視性（シャドウマップで確認）
        float visibility = SampleShadow(pos);

        // 散乱
        float cosTheta   = dot(rayDir, -sunDir);
        float phase      = HenyeyGreenstein(cosTheta, 0.3);
        scattered       += sunColor * visibility * phase * stepSize;

        pos += rayDir * stepSize;
    }
    return scattered;
}
```

---

## 透過・半透明（Order Independent Transparency）

通常の Alpha Blend はソート依存。OIT でソートなし透明描画を実現。

### Weighted Blended OIT（Morgan McGuire 2013）
```hlsl
// 透明パス（アキュムレーションバッファへ）
struct OITOut {
    float4 accum  : SV_Target0; // 重み付き色の累積
    float  reveal : SV_Target1; // 累積透明度
};

OITOut TransparentPS(VSOutput input) {
    float4 color  = SampleAlbedo(input.uv);
    float  alpha  = color.a;
    float  depth  = input.position.z;

    // 深度・アルファに基づく重み
    float w = clamp(pow(min(1.0, alpha * 10.0) + 0.01, 3.0)
              * 1e8 * pow(1.0 - depth * 0.9, 3.0), 1e-2, 3e3);

    OITOut output;
    output.accum  = float4(color.rgb * alpha, alpha) * w;
    output.reveal = alpha; // Blend: DST * (1 - SRC)
    return output;
}

// コンポジットパス
float4 OITCompositePS(float2 uv : TEXCOORD0) : SV_Target {
    float4 accum  = accumBuffer.Sample(samp, uv);
    float  reveal = revealBuffer.Sample(samp, uv).r;

    float3 avgColor = accum.rgb / max(accum.a, 1e-5);
    float  alpha    = 1.0 - reveal;

    return float4(avgColor, alpha);
}
```

---

## Subsurface Scattering（SSS）

光が半透明素材（肌・蝋・玉石）の内部で散乱する効果。

### Pre-Integrated Skin Shading（近似）
```hlsl
// 曲率と NdotL から SSS テクスチャをルックアップ
float curvature = length(fwidth(worldNormal)) / length(fwidth(worldPos));
float2 sssUV    = float2(NdotL * 0.5 + 0.5, curvature);
float3 sssBRDF  = sssSkin.Sample(samp, sssUV).rgb;

float3 lighting = lightColor * albedo * sssBRDF;
```

---

## 関連ドキュメント
- [13-ambient-occlusion.md](13-ambient-occlusion.md) - SSAO（Deferred と統合）
- [15-compute-shaders.md](15-compute-shaders.md) - タイルカリング（Compute Shader）
- [08-pbr-theory.md](08-pbr-theory.md) - PBR BRDF（Lighting Pass で使用）
