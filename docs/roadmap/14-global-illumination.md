# Roadmap: Phase 14 - Global Illumination

**プロジェクト**: `D:/dev/shader/global-illumination/`
**API**: Direct3D 12 + DXR
**目標**: Diffuse および Specular の間接光をリアルタイムで実装し、UE5 Lumen 相当のシステムを理解・自作する

---

## 習得する概念

| 概念 | 関連ドキュメント |
|---|---|
| Global Illumination 全般 | [20-global-illumination.md](../concepts/20-global-illumination.md) |
| Spherical Harmonics | [20-global-illumination.md](../concepts/20-global-illumination.md) |
| Ray Tracing (DXR) | [roadmap/07-ray-tracing.md](07-ray-tracing.md) |
| IBL（Irradiance Map / BRDF LUT） | [09-ibl.md](../concepts/09-ibl.md) |
| Compute Shader | [15-compute-shaders.md](../concepts/15-compute-shaders.md) |

---

## フェーズ分け

### フェーズ 14-1: Baked SH Probe GI（静的 GI 基礎）

最もシンプルな間接光：シーン内に格子状にプローブを配置し、オフラインで SH 係数をベイク。

**実装項目**:
- `ProbeGrid`：ワールド空間のプローブ格子（位置と SH 係数 27 floats/probe）
- オフライン SH ベイク（各プローブ位置から CubeMap レンダリング → SH 射影）
- 実行時: 頂点法線でプローブを三線形補間し Irradiance を取得
- アーティスト向け: プローブ Visualizer（デバッグ球描画）

```hlsl
// SH Irradiance の評価（L2 SH, 9 係数）
float3 EvalSHIrradiance(float3 N, float3 shR[9], float3 shG[9], float3 shB[9]) {
    float y[9];
    EvalSHBasis(N, y);

    float3 result;
    result.r = dot(float4(y[0],y[1],y[2],y[3]), float4(shR[0].x,shR[1].x,shR[2].x,shR[3].x))
             + dot(float4(y[4],y[5],y[6],y[7]), float4(shR[4].x,shR[5].x,shR[6].x,shR[7].x))
             + y[8] * shR[8].x;
    // g, b も同様...
    return max(result, 0.0);
}

// 三線形プローブ補間
float3 SampleProbeGrid(float3 worldPos, float3 N,
                        StructuredBuffer<SHProbeData> probes,
                        ProbeGridInfo grid) {
    float3 local  = (worldPos - grid.origin) / grid.spacing;
    int3   base   = (int3)floor(local);
    float3 weight = local - base;

    float3 irr = 0;
    for (int dz = 0; dz <= 1; dz++)
    for (int dy = 0; dy <= 1; dy++)
    for (int dx = 0; dx <= 1; dx++) {
        int3  idx = clamp(base + int3(dx,dy,dz), 0, grid.dims - 1);
        float w   = ((dx ? weight.x : 1-weight.x)
                   * (dy ? weight.y : 1-weight.y)
                   * (dz ? weight.z : 1-weight.z));
        SHProbeData p = probes[FlatIndex(idx, grid.dims)];
        irr += w * EvalSHIrradiance(N, p.shR, p.shG, p.shB);
    }
    return irr;
}
```

---

### フェーズ 14-2: DDGI（Dynamic Diffuse Global Illumination）

NVIDIA Research の DDGI を D3D12 + DXR で実装する。

**実装項目**:
- DXR セットアップ（Phase 7 を前提）
- Probe Ray Cast CS: 各プローブから 128 本のランダムレイをキャスト
- RayGen Shader: プローブから出るレイのワールド方向を計算
- Hit Shader: ヒット点の直接光 + 前フレーム Irradiance を返す（1 バウンス）
- Irradiance 更新 CS: Octahedral テクスチャに指数移動平均でブレンド (α ≈ 0.1)
- Visibility 更新 CS: 距離の平均・分散を Octahedral テクスチャに保存
- 適用 PS: Trilinear 補間 + Chebyshev Visibility Weight

```hlsl
// Probe Irradiance テクスチャのレイアウト
// PROBE_COUNT × 8px × 8px の Texture2DArray（Octahedral マッピング）

// Irradiance 更新（Blending CS）
[numthreads(8, 8, 1)]
void UpdateIrradianceCS(uint3 tid : SV_DispatchThreadID) {
    uint2  probeTexel = tid.xy;  // プローブ内 UV テクセル [0,7]
    uint   probeIdx   = tid.z;

    float2 oct        = (probeTexel + 0.5) / 8.0;
    float3 dir        = OctaDecode(oct);

    float3 newIrr     = 0;
    for (uint ray = 0; ray < NUM_RAYS_PER_PROBE; ray++) {
        float3 rayDir      = probeRayDirs[ray];
        float  weight      = max(0, dot(dir, rayDir));
        float3 rayRadiance = probeRayRadiances[probeIdx * NUM_RAYS_PER_PROBE + ray];
        newIrr += weight * rayRadiance;
    }
    newIrr *= 2.0 * PI / NUM_RAYS_PER_PROBE;

    float3 prevIrr = probeIrradianceTex[uint3(probeTexel, probeIdx)].rgb;
    float3 blended = lerp(prevIrr, newIrr, DDGI_BLEND_ALPHA);
    probeIrradianceTex[uint3(probeTexel, probeIdx)] = float4(blended, 1);
}
```

---

### フェーズ 14-3: VXGI（Voxel Cone Tracing）

シーンを 3D テクスチャにボクセル化し、コーントレーシングで間接光を計算。

**実装項目**:
- Voxelization Pass: 三角形を `RWTexture3D<uint>` にアトミック書き込み（CAS）
- Voxel Mipmap 生成 CS: 3D テクスチャの Anisotropic Mipmap（6 面方向別）
- Diffuse Cone Tracing PS: 半球上 6〜8 本のコーンをトレース
- Specular Cone Tracing PS: Roughness に応じたコーン幅でトレース
- Radiance Injection: Light から Voxel への注入（Direct Light のみ）

```hlsl
// Diffuse Cone Tracing（6本のコーン）
float3 VoxelConeTraceDiffuse(float3 worldPos, float3 N) {
    // 法線半球上の 6 方向コーン（60° 開き角）
    float3 dirs[6] = {
        float3(0, 1, 0),
        float3(0, 0.5, 0.866),   float3(0.823, 0.5, 0.268),
        float3(0.509, 0.5,-0.702),float3(-0.509,0.5,-0.702),
        float3(-0.823,0.5, 0.268)
    };
    float  weights[6] = { 0.25, 0.15, 0.15, 0.15, 0.15, 0.15 };

    float3 tangent, bitangent;
    ComputeTangentBasis(N, tangent, bitangent);
    float3x3 TBN = float3x3(tangent, bitangent, N);

    float3 result = 0;
    for (int i = 0; i < 6; i++) {
        float3 coneDir = mul(dirs[i], TBN);
        result += weights[i] * ConeTrace(worldPos + N * VOXEL_SIZE, coneDir,
                                          60.0 * DEG2RAD, voxelMipmap);
    }
    return result;
}
```

---

### フェーズ 14-4: Screen Space GI（SSGI）

スクリーン空間情報だけで間接光を近似する軽量な手法。Phase 5 の SSR と対になる。

**実装項目**:
- Diffuse GI: コサイン重み半球サンプリング + スクリーンスペースレイマーチ
- ヒット点のラジアンス（Scene Color Buffer）を収集
- Temporal Accumulation（履歴バッファ + Reprojection で安定化）
- Spatial Blur（ガウシアン + 法線・深度ガイド）

```hlsl
float3 ComputeSSGI(float2 uv, float3 worldPos, float3 N, int numSamples) {
    float3 indirect = 0;
    float  totalWeight = 0;

    for (int i = 0; i < numSamples; i++) {
        float2 xi  = Hammersley(i + frameIndex * numSamples, numSamples * MAX_FRAMES);
        float3 dir = CosineSampleHemisphere(xi);
        dir        = TangentToWorld(dir, N);

        // スクリーンスペースレイマーチ（最大 32 ステップ）
        for (int step = 1; step <= 32; step++) {
            float3 samplePos = worldPos + dir * (0.05 * step * step);
            float2 sampleUV  = WorldToScreenUV(samplePos, viewProj);

            if (any(sampleUV < 0) || any(sampleUV > 1)) break;

            float  sampleDepth = LinearizeDepth(depthTex.SampleLevel(p, sampleUV, 0).r);
            float  rayDepth    = GetLinearDepth(samplePos);

            if (rayDepth > sampleDepth && rayDepth - sampleDepth < 0.5) {
                float3 radiance = sceneColor.SampleLevel(linearSamp, sampleUV, 0).rgb;
                float  weight   = dot(dir, N);
                indirect       += radiance * weight;
                totalWeight    += weight;
                break;
            }
        }
    }
    return totalWeight > 0 ? indirect / totalWeight : 0;
}
```

---

### フェーズ 14-5: ReSTIR GI

パストレーシングベースの GI を Reservoir サンプリングで実用的な速度に高速化する。
Phase 7 の Path Tracing を前提とする。

**実装項目**:
- `Reservoir` 構造体（最初の経路サンプルを WRS で選択）
- Initial Sampling CS: 各ピクセルで 1 経路をサンプリング
- Temporal Reuse CS: 前フレームのリザーバーとマージ
- Spatial Reuse CS: 近隣 8 ピクセルとリザーバーをマージ
- Final Shading PS: 選択されたサンプルで BRDF 評価

```hlsl
struct Reservoir {
    float3 hitPoint;   // 選択されたサンプル
    float3 hitNormal;
    float3 hitRadiance;
    float  W;          // 非正規化重み
    float  M;          // 試行回数
    float  wSum;       // 重みの総和
};

// Reservoir へのサンプル更新 (Weighted Reservoir Sampling)
bool UpdateReservoir(inout Reservoir r, float3 pos, float3 N, float3 rad,
                     float w, float xi) {
    r.wSum += w;
    r.M    += 1;
    if (xi < w / r.wSum) {
        r.hitPoint    = pos;
        r.hitNormal   = N;
        r.hitRadiance = rad;
        return true;
    }
    return false;
}
```

---

### フェーズ 14-6: Lumen 風ハイブリッド GI

Phase 14-1〜14-5 の技術を組み合わせ、距離に応じて使い分けるハイブリッドシステムを設計する。

**実装項目**:
- 近距離: SSGI（Screen Traces）
- 中距離: DDGI プローブ（動的更新）
- 遠距離: SH スカイライト
- Surface Cache: オブジェクト表面の Radiance をテクスチャにキャッシュ
- Temporal Accumulation + A-trous フィルタでデノイズ

```
[GI 系統の距離ブレンド]
  0〜2m  : SSGI（スクリーン空間、最も精度が高い）
  2〜20m : DDGI プローブ補間
  20m〜  : SH Skylight（固定・低コスト）
  各層をスムーズに補間 (smoothstep + depth)
```

---

## ファイル構成（完成時）

```
global-illumination/
├── CMakeLists.txt
├── src/
│   ├── ProbeGrid.cpp/.h          ← プローブ格子管理
│   ├── DDGISystem.cpp/.h         ← DDGI 更新・描画
│   ├── VoxelizerCS.cpp/.h        ← Voxelization パス
│   ├── SSGIPass.cpp/.h           ← Screen Space GI
│   ├── ReSTIRGI.cpp/.h           ← ReSTIR リザーバー
│   └── HybridGIRenderer.cpp/.h  ← 統合ハイブリッド
└── shaders/
    ├── sh_project_cs.hlsl        ← SH 射影
    ├── sh_irradiance_ps.hlsl     ← SH 適用
    ├── ddgi_raygen.hlsl          ← DXR RayGen
    ├── ddgi_hit.hlsl             ← ClosestHit
    ├── ddgi_update_cs.hlsl       ← Irradiance 更新
    ├── voxelize_cs.hlsl
    ├── voxel_mipmap_cs.hlsl
    ├── voxel_cone_trace_ps.hlsl
    ├── ssgi_cs.hlsl
    ├── restir_initial_cs.hlsl
    ├── restir_temporal_cs.hlsl
    ├── restir_spatial_cs.hlsl
    └── gi_composite_ps.hlsl
```

---

## 確認チェックリスト

- [ ] SH プローブから間接光がキャラクターに適用される
- [ ] DDGI でシーン変化（ライト移動）に間接光が追従する
- [ ] VXGI でグロスなスペキュラー間接反射が出る
- [ ] SSGI が近距離のカラーブリードを正しく表現する
- [ ] ReSTIR GI で 1 SPP でもノイズが少ない収束が得られる
- [ ] ハイブリッドシステムで距離によるブレンドに継ぎ目がない

---

## 関連ドキュメント
- [13-slang-rewrite.md](13-slang-rewrite.md) - 前フェーズ（Slang 移行）
- [15-debug-rendering.md](15-debug-rendering.md) - 次フェーズ
- [../concepts/20-global-illumination.md](../concepts/20-global-illumination.md)
- [07-ray-tracing.md](07-ray-tracing.md) - DXR 前提
