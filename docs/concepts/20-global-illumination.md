# 概念: Global Illumination（グローバルイルミネーション）

## 概要

Global Illumination (GI) は、光が複数の表面でバウンスしながら伝播する現象を扱うレンダリング技術の総称。
Direct Lighting（光源からの直接照明）だけでなく、**Indirect Lighting**（間接照明：他の表面から反射してきた光）を考慮することで、
物理的に正確なシーンの明暗・カラーブリードを実現する。

UE5 の Lumen、Path Tracer、ゲームエンジン自作の照明システム設計の基盤となる概念。

---

## レンダリング方程式における GI

```
Lo(x, ω) = Le(x, ω) + ∫Ω fr(x, ω', ω) Li(x, ω') (ω' · n) dω'
```

- `Le` : 自己発光
- `fr` : BRDF
- `Li` : **入射光**（= Direct + Indirect）
- Indirect Light は再帰的：他の点 `y` の `Lo(y, -ω')` が `Li(x, ω')` になる

完全な GI はパストレーシングで解くが、リアルタイムでは**近似手法**を用いる。

---

## Spherical Harmonics（球面調和関数）

### 概要

低周波の環境照明（特に Diffuse Irradiance）を少数の係数（L2 SH で 9 係数×RGB = 27 float）で表現する。
Irradiance Probe・スカイライト・キャラクターへの間接光など、幅広く使われる。

### SH 基底関数（L0〜L2）

```
L0: Y_0^0  = 1/(2√π)
L1: Y_1^{-1} = √(3/(4π)) · y
    Y_1^0  = √(3/(4π)) · z
    Y_1^1  = √(3/(4π)) · x
L2: Y_2^{-2} = √(15/(4π))  · xy
    Y_2^{-1} = √(15/(4π))  · yz
    Y_2^0  = √(5/(16π))    · (3z²-1)
    Y_2^1  = √(15/(4π))    · xz
    Y_2^2  = √(15/(16π))   · (x²-y²)
```

### HLSL: SH Irradiance エンコード（環境マップ → SH 係数）

```hlsl
// Compute Shader: 環境 CubeMap を L2 SH に射影
// 27 coefficients (9 per channel)

groupshared float3 sh_accum[9][GROUP_SIZE];

[numthreads(GROUP_SIZE, 1, 1)]
void ProjectEnvMapToSHCS(uint id : SV_DispatchThreadID) {
    float3 shCoeffs[9] = (float3[9])0;

    // 球面上をサンプリング
    for (uint i = id; i < NUM_SAMPLES; i += TOTAL_THREADS) {
        float2 xi  = Hammersley(i, NUM_SAMPLES);
        float3 dir = SampleSphere(xi);
        float3 radiance = envCubeMap.SampleLevel(linearSampler, dir, 0).rgb;

        // SH 基底関数で射影
        float y[9];
        EvalSH(dir, y);
        for (int b = 0; b < 9; b++)
            shCoeffs[b] += radiance * y[b];
    }
    // ... (Reduce and output)
}

// SH 基底関数評価
void EvalSH(float3 n, out float y[9]) {
    y[0] = 0.282095;
    y[1] = 0.488603 * n.y;
    y[2] = 0.488603 * n.z;
    y[3] = 0.488603 * n.x;
    y[4] = 1.092548 * n.x * n.y;
    y[5] = 1.092548 * n.y * n.z;
    y[6] = 0.315392 * (3.0 * n.z * n.z - 1.0);
    y[7] = 1.092548 * n.x * n.z;
    y[8] = 0.546274 * (n.x * n.x - n.y * n.y);
}
```

### HLSL: SH Irradiance デコード（シェーダーでの使用）

```hlsl
// L2 SH から法線 N の Irradiance を復元
// 事前計算済み SH 係数（27 floats = 9 × RGB）を定数バッファ経由で渡す

cbuffer SHData : register(b1) {
    float4 shR[9];  // Red channel (float3 の係数をパック)
    float4 shG[9];
    float4 shB[9];
};

float3 EvalSHIrradiance(float3 N) {
    float y[9];
    EvalSH(N, y);

    float r = dot(float4(y[0],y[1],y[2],y[3]), shR[0])
            + dot(float4(y[4],y[5],y[6],y[7]), shR[1])
            + y[8] * shR[2].x;
    float g = /* 同様 */;
    float b = /* 同様 */;
    return max(float3(r, g, b), 0.0);
}

// ピクセルシェーダーでの使用
float3 indirectDiffuse = EvalSHIrradiance(worldNormal) * albedo / PI;
```

---

## ライトプローブ（Irradiance Probe）システム

シーン中に格子状・テトラへドラル状に配置した**プローブ**が、その位置での Irradiance（SH 係数）を事前計算またはリアルタイムで保持する。
サーフェスはプローブを補間して間接光を取得する。

### プローブ補間

```hlsl
// 三線形補間（Probe Grid 内）
float3 InterpolateProbes(float3 worldPos, StructuredBuffer<ProbeData> probes,
                         float3 gridOrigin, float3 gridSpacing) {
    float3 local = (worldPos - gridOrigin) / gridSpacing;
    int3   base  = (int3)floor(local);
    float3 frac  = local - base;

    float3 irradiance = 0;
    for (int dz = 0; dz <= 1; dz++)
    for (int dy = 0; dy <= 1; dy++)
    for (int dx = 0; dx <= 1; dx++) {
        int3   idx    = base + int3(dx, dy, dz);
        float  weight = ((dx ? frac.x : 1-frac.x)
                       * (dy ? frac.y : 1-frac.y)
                       * (dz ? frac.z : 1-frac.z));
        ProbeData p = probes[FlatIndex(idx)];
        irradiance += weight * EvalSHIrradiance(p.shCoeffs, worldNormal);
    }
    return irradiance;
}
```

---

## DDGI（Dynamic Diffuse Global Illumination）

NVIDIA Research (2019) が提案した、リアルタイム動的 GI の実用的な手法。
プローブから**ランダムレイをキャスト**し、衝突点の直接光を蓄積して Irradiance と Visibility を Octahedron マップに格納。
毎フレーム段階的に更新することでシーン変化に追従する。

```
[DDGI の更新フロー]
  1. Probe Ray Cast（CS/DXR）: 各プローブから N 本（128 本程度）のレイ
  2. Radiance 計算: ヒット点での Direct Light を評価
  3. Irradiance 更新: Octahedral テクスチャに Blending（α = 0.01〜0.2）
  4. Visibility 更新: 遮蔽距離を Octahedral テクスチャに保存
  5. Pixel Shader: プローブを Trilinear 補間、Chebyshev Visibility で Weight
```

### プローブ Irradiance テクスチャ（Octahedral Mapping）

```hlsl
// Octahedral 方向エンコード（1プローブ = 8x8 テクセル）
float2 OctaEncode(float3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    float2 oct = n.z >= 0 ? n.xy : (1.0 - abs(n.yx)) * sign(n.xy);
    return oct * 0.5 + 0.5;
}

float3 OctaDecode(float2 oct) {
    oct = oct * 2.0 - 1.0;
    float3 n = float3(oct.x, oct.y, 1.0 - abs(oct.x) - abs(oct.y));
    float t = saturate(-n.z);
    n.xy += sign(n.xy) * -t;
    return normalize(n);
}

// プローブからの Irradiance サンプリング（Visibility Weight 付き）
float3 SampleProbeIrradiance(float3 worldPos, float3 N,
                              Texture2DArray irradianceTex,
                              Texture2DArray visibilityTex,
                              int probeIdx) {
    float2 probeUV = OctaEncode(N);

    // Visibility（Chebyshev）で自己遮蔽対策
    float2 vis = visibilityTex.SampleLevel(linearSampler,
        float3(probeUV, probeIdx), 0).rg;
    float  mean     = vis.r;
    float  variance = abs(vis.g - mean * mean);
    float  dist     = length(worldPos - probePositions[probeIdx]);
    float  chebyshev = variance / (variance + max(dist - mean, 0) * max(dist - mean, 0));
    float  weight   = max(chebyshev, 0.05);

    float3 irradiance = irradianceTex.SampleLevel(linearSampler,
        float3(probeUV, probeIdx), 0).rgb;
    return irradiance * weight;
}
```

---

## Voxel Cone Tracing（VXGI）

シーンを 3D ボクセルグリッドに格納し、**コーントレーシング**で間接光を近似する。
Diffuse（広いコーン）・Specular（狭いコーン）の両方に対応できる。

```
[VXGI フロー]
  1. Voxelization Pass（CS）: シーンジオメトリをボクセルグリッドに書き込み
  2. Mipmap 生成（CS）: ボクセルテクスチャの階層ミップ
  3. Cone Tracing（PS/CS）: ヒット点から Irradiance/Specular コーンをトレース
```

### ボクセル化 Compute Shader（概要）

```hlsl
// RWTexture3D に書き込み（アトミック加算でブレンド）
RWTexture3D<uint> voxelAlbedo : register(u0);

[numthreads(64, 1, 1)]
void VoxelizeCS(uint id : SV_DispatchThreadID) {
    // 三角形をボクセルグリッドに保守的にラスタライズ
    // ...
    float3 color = SampleAlbedo(tri.uv);
    uint3  voxelIdx = WorldToVoxel(tri.worldPos);

    // アトミックで書き込み（R8G8B8A8 パック）
    uint packed = PackColor(float4(color, 1.0));
    InterlockedMax(voxelAlbedo[voxelIdx], packed);
}
```

### Cone Tracing シェーダー

```hlsl
// Diffuse GI: 半球上に複数のコーン（6本程度）をトレース
float3 VoxelConeTraceDiffuse(float3 worldPos, float3 N,
                              Texture3D voxelMip, SamplerState samp) {
    const int   NUM_CONES   = 6;
    const float CONE_ANGLE  = 60.0 * (PI / 180.0);
    float3      result      = 0;

    for (int i = 0; i < NUM_CONES; i++) {
        float3 coneDir = TangentToWorld(CONE_DIRS[i], N);
        result += ConeTrace(worldPos, coneDir, CONE_ANGLE, voxelMip, samp)
                * dot(coneDir, N);
    }
    return result / NUM_CONES;
}

float3 ConeTrace(float3 origin, float3 dir, float halfAngle,
                 Texture3D voxelMip, SamplerState samp) {
    float3 accum      = 0;
    float  alpha      = 0;
    float  dist       = VOXEL_SIZE; // 開始距離（自己交差回避）
    float  maxDist    = 20.0;

    while (dist < maxDist && alpha < 0.95) {
        float  diameter = 2.0 * dist * tan(halfAngle);
        float  mipLevel = log2(diameter / VOXEL_SIZE);
        float3 samplePos = (origin + dir * dist - voxelGridCenter) / voxelGridHalfSize;
        samplePos = samplePos * 0.5 + 0.5;

        float4 voxel = voxelMip.SampleLevel(samp, samplePos, mipLevel);
        accum += (1.0 - alpha) * voxel.a * voxel.rgb;
        alpha += (1.0 - alpha) * voxel.a;
        dist  += diameter * 0.5; // ステップ = コーン直径の半分
    }
    return accum;
}
```

---

## Screen Space GI（SSGI）

スクリーンスペース内の情報だけで間接光を近似する手法。
UE5 の Lumen の Screen Traces がこれに近い。SSAO の拡張版ともいえる。

```hlsl
// SSGI: スクリーン空間でレイマーチして隣接サーフェスのラジアンスを収集
float3 ComputeSSGI(float2 uv, float3 worldPos, float3 N,
                   Texture2D sceneColor, Texture2D depthTex,
                   float4x4 viewProj, float4x4 invViewProj) {
    float3 indirectLight = 0;
    int    numSamples    = 8;

    for (int i = 0; i < numSamples; i++) {
        // コサイン重み付きサンプル方向
        float2 xi  = Hammersley(i, numSamples);
        float3 dir = CosineSampleHemisphere(xi);
        dir        = TangentToWorld(dir, N);

        // スクリーンスペースレイマーチ
        float3 rayPos   = worldPos + dir * 0.1;
        float3 hitColor = 0;
        bool   hit      = false;

        for (int step = 0; step < 16; step++) {
            rayPos += dir * (0.1 * (step + 1));
            float4 clipPos = mul(viewProj, float4(rayPos, 1));
            float2 rayUV   = clipPos.xy / clipPos.w * float2(0.5, -0.5) + 0.5;

            if (any(rayUV < 0) || any(rayUV > 1)) break;

            float sceneDepth = ReconstructLinearDepth(depthTex.SampleLevel(pointSamp, rayUV, 0).r);
            float rayDepth   = clipPos.z / clipPos.w;
            float linearRay  = ReconstructLinearDepth(rayDepth);

            if (linearRay > sceneDepth && linearRay - sceneDepth < 0.3) {
                hitColor = sceneColor.SampleLevel(linearSamp, rayUV, 0).rgb;
                hit      = true;
                break;
            }
        }
        if (hit) indirectLight += hitColor * dot(dir, N);
    }
    return indirectLight / numSamples;
}
```

---

## LPV（Light Propagation Volumes）

CryEngine が提案した手法。RSM（Reflective Shadow Map）から Virtual Point Light を注入し、
3D ボリューム（SH 格納）を通じて光を伝播させる。

```
[LPV フロー]
  1. RSM（Reflective Shadow Map）生成: 通常のシャドウマップ + Flux/Normal も保存
  2. VPL 注入 CS: RSM の各テクセル → LPV ボリュームの SH テクスチャに寄与
  3. 伝播 CS: 隣接 6 方向へ SH 係数を伝播（複数回イテレーション）
  4. 適用 PS: LPV から Irradiance をサンプリング
```

---

## ReSTIR GI

2021 年 SIGGRAPH の手法。**Reservoir** ベースの重点サンプリングで、
少ないサンプル数でも品質の高い GI を実現する。パストレーシングと組み合わせて使用。

```
[ReSTIR GI の核心]
  - 各ピクセルが "Reservoir"（重み付きサンプルプール）を持つ
  - 空間的再利用: 近隣ピクセルの Reservoir をマージ（Spatial Reuse）
  - 時間的再利用: 前フレームの Reservoir を利用（Temporal Reuse）
  → 少数サンプルでも収束が速い
```

---

## Lumen（UE5）のアーキテクチャ概要

UE5 の Lumen は複数の技術を階層的に組み合わせたハイブリッド GI：

```
[近距離]  Screen Traces（SSGI 相当）
[中距離]  Mesh Distance Field Traces（SDF でのレイキャスト）
[遠距離]  Lumen Scene（Surface Cache + Voxel Lighting）
[統合]    Irradiance Probe（プローブ格子）で最終補間
[デノイズ] Temporal Accumulation + Spatial Filter
```

---

## 比較表

| 手法 | 動的シーン | GPU 負荷 | 品質 | 用途 |
|---|---|---|---|---|
| Baked Lightmap | × | 低（実行時） | 高 | 静的シーン |
| SH Probe（ベイク） | 限定的 | 低 | 中 | キャラ間接光 |
| DDGI | ○ | 中 | 高（Diffuse） | AAA ゲーム |
| VXGI | ○ | 高 | 中〜高 | エンジン特化 |
| SSGI | ○ | 低〜中 | 中（スクリーン限界） | Mobile / Web |
| LPV | ○ | 中 | 低〜中 | レガシー |
| ReSTIR GI | ○ | 高（DXR） | 高 | 次世代 |
| Lumen | ○ | 高 | 高 | UE5 専用 |

---

## 関連ドキュメント

- [08-pbr-theory.md](08-pbr-theory.md) — BRDF・レンダリング方程式
- [09-ibl.md](09-ibl.md) — Irradiance Map・BRDF LUT
- [14-advanced-rendering.md](14-advanced-rendering.md) — Deferred Shading・SSR
- [roadmap/14-global-illumination.md](../roadmap/14-global-illumination.md) — GI 実装ロードマップ
