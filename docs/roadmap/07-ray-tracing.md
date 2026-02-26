# Roadmap: Phase 7 - Ray Tracing

**プロジェクト**: `D:/dev/shader/ray-tracing/`
**API**: Direct3D 12 + DXR（DirectX Raytracing）
**目標**: DXR パイプラインを構築し、ハイブリッドレンダリングに統合する

---

## 習得する概念

| 概念 | 関連ドキュメント |
|---|---|
| RTAO・RT シャドウ・RT 反射 | [14-advanced-rendering.md](../concepts/14-advanced-rendering.md) |
| 最適化（BVH・Wave Intrinsics） | [16-optimization.md](../concepts/16-optimization.md) |
| Compute Shader（デノイズ） | [15-compute-shaders.md](../concepts/15-compute-shaders.md) |

---

## DXR パイプラインの概念図

```
CPU が TraceRay() を Dispatch
         ↓
Ray Generation Shader（各ピクセルでレイを生成）
         ↓
BVH トラバーサル（ハードウェア）
         ↓
┌────────────────────────────────┐
│ Intersection         ← AABBプリミティブ用（省略可）
│ Any Hit              ← 透明テスト用（省略可）
│ Closest Hit          ← 最初の交差点でシェーディング
│ Miss                 ← 交差なし（スカイカラー等）
└────────────────────────────────┘
```

---

## フェーズ分け

### フェーズ 7-1: DXR 環境セットアップ

**実装項目**:
- `D3D12_RAYTRACING_TIER` の確認（Tier 1.0 以上必要）
- `ID3D12Device5` の取得（DXR インターフェース）
- Bottom-Level Acceleration Structure（BLAS）の構築
- Top-Level Acceleration Structure（TLAS）の構築
- Shader Binding Table（SBT）の設計と構築
- State Object（`D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE`）の作成

```cpp
// BLAS 構築（三角形メッシュ）
D3D12_RAYTRACING_GEOMETRY_DESC geomDesc = {};
geomDesc.Type  = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
geomDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
geomDesc.Triangles.VertexBuffer.StartAddress  = vertexBufferGpuAddr;
geomDesc.Triangles.VertexBuffer.StrideInBytes = sizeof(Vertex);
geomDesc.Triangles.VertexFormat   = DXGI_FORMAT_R32G32B32_FLOAT;
geomDesc.Triangles.VertexCount    = vertexCount;
geomDesc.Triangles.IndexBuffer    = indexBufferGpuAddr;
geomDesc.Triangles.IndexFormat    = DXGI_FORMAT_R32_UINT;
geomDesc.Triangles.IndexCount     = indexCount;

// TLAS 構築（インスタンスリスト）
D3D12_RAYTRACING_INSTANCE_DESC instance = {};
XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(instance.Transform), worldMatrix);
instance.InstanceMask = 0xFF;
instance.AccelerationStructure = blasGpuAddr;
```

---

### フェーズ 7-2: 最初のレイジェネレーションシェーダー

**実装項目**:
- `lib_6_3` シェーダープロファイルでコンパイル（`dxc -T lib_6_3`）
- Ray Generation Shader: 各ピクセルにプライマリレイを発射
- Miss Shader: 空のスカイカラーを返す
- Closest Hit Shader: 最近傍ヒットで拡散色を返す

```hlsl
// RT シェーダーファイル全体を 1 ファイルに記述
RaytracingAccelerationStructure scene  : register(t0);
RWTexture2D<float4>              output : register(u0);

[shader("raygeneration")]
void RayGen() {
    uint2 idx = DispatchRaysIndex().xy;
    uint2 dim = DispatchRaysDimensions().xy;

    float2 ndc = (float2(idx) + 0.5) / float2(dim) * 2.0 - 1.0;
    float3 dir = normalize(float3(ndc.x, -ndc.y, -1.0)); // 簡易カメラ

    RayDesc ray;
    ray.Origin    = cameraPos;
    ray.Direction = dir;
    ray.TMin      = 0.001;
    ray.TMax      = 1000.0;

    RayPayload payload = { float3(0,0,0) };
    TraceRay(scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xFF, 0, 1, 0, ray, payload);
    output[idx] = float4(payload.color, 1.0);
}

[shader("miss")]
void Miss(inout RayPayload payload) {
    payload.color = float3(0.2, 0.5, 0.8); // スカイカラー
}

[shader("closesthit")]
void ClosestHit(inout RayPayload payload, BuiltInTriangleIntersectionAttributes attr) {
    float3 bary = float3(1 - attr.barycentrics.x - attr.barycentrics.y,
                         attr.barycentrics.x, attr.barycentrics.y);
    payload.color = bary; // 重心座標を色として可視化
}
```

---

### フェーズ 7-3: RTAO（Ray Traced Ambient Occlusion）

**実装項目**:
- G-Buffer から World Position + Normal を取得
- 半球上のランダムレイを N 本発射（Cosine Weighted Sampling）
- ヒットした場合 0（遮蔽）、ミスの場合 1（照射）を累積
- 単純な空間 / 時間フィルタリングでノイズ除去
- Phase 5 の Deferred パイプラインに統合

```hlsl
[shader("raygeneration")]
void RTAORayGen() {
    uint2 idx = DispatchRaysIndex().xy;
    float3 worldPos = ReconstructWorldPos(idx);
    float3 N        = gbufNormal[idx].xyz * 2.0 - 1.0;

    float occlusion = 0.0;
    for (int i = 0; i < NUM_AO_RAYS; i++) {
        float3 dir = CosineSampleHemisphere(N, RandomFloat2(idx, i, frameIndex));

        RayDesc ray;
        ray.Origin    = worldPos + N * 0.01; // 自己交差防止オフセット
        ray.Direction = dir;
        ray.TMin      = 0.01;
        ray.TMax      = AO_RADIUS;

        ShadowPayload payload = { 0.0 }; // 0=遮蔽, 1=照射
        TraceRay(scene,
            RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
            RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
            0xFF, 1, 1, 1, ray, payload);
        occlusion += payload.hit;
    }
    aoOutput[idx] = occlusion / float(NUM_AO_RAYS);
}

[shader("miss")]
void RTAOMiss(inout ShadowPayload payload) { payload.hit = 1.0; }
// Closest Hit 不要（RAY_FLAG_SKIP_CLOSEST_HIT_SHADER のため）
```

---

### フェーズ 7-4: RT シャドウ

**実装項目**:
- 各ピクセルから光源へ Shadow Ray を発射
- エリアライト: 光源面積内のランダム点に向けてレイを発射 → ソフトシャドウ
- `RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH` で最速判定
- Any Hit Shader で透明オブジェクトを考慮

```hlsl
// Shadow Ray: 高速化フラグを活用
TraceRay(scene,
    RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |  // 最初の交差で終了
    RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,            // Closest Hit 不要
    0xFF, 0, 1, 0, shadowRay, shadowPayload);
```

---

### フェーズ 7-5: RT 反射

**実装項目**:
- Reflect() で反射方向を計算してレイを発射
- Roughness に応じた Cone Sampling（複数レイ）
- ヒット後のシェーディング（再帰 1 バウンス）
- SSR フォールバックとのブレンド（Near Screen: SSR、Out of Screen: RT）

```hlsl
[shader("closesthit")]
void ReflectionHit(inout ReflectionPayload payload, BuiltInTriangleIntersectionAttributes attr) {
    // ヒット面の PBR マテリアルを評価
    uint2 hitUV = GetHitUV(PrimitiveIndex(), attr.barycentrics);
    float3 albedo    = albedoTex[PrimitiveIndex()].Sample(samp, hitUV).rgb;
    float3 hitNormal = GetInterpolatedNormal(PrimitiveIndex(), attr.barycentrics);

    // 簡易ライティング（再帰せず直接光のみ）
    float NdotL = max(dot(hitNormal, -sunDir), 0.0);
    payload.color = albedo * sunColor * NdotL;
}
```

---

### フェーズ 7-6: パストレーシング（1 バウンス〜N バウンス）

**実装項目**:
- Cosine Weighted Hemisphere Sampling
- Russian Roulette（ロシアンルーレット打ち切り）
- MIS（Multiple Importance Sampling）
- 再帰（`TraceRay` 内で `TraceRay` は不可 → ループで実装）

```hlsl
[shader("raygeneration")]
void PathTraceRayGen() {
    float3 throughput = float3(1, 1, 1);
    float3 Lo         = float3(0, 0, 0);
    RayDesc ray       = GenerateCameraRay(DispatchRaysIndex().xy);

    [loop]
    for (int depth = 0; depth < MAX_BOUNCES; depth++) {
        PathPayload payload = {};
        TraceRay(scene, RAY_FLAG_NONE, 0xFF, 0, 1, 0, ray, payload);

        if (payload.miss) {
            Lo += throughput * SampleEnvmap(ray.Direction);
            break;
        }

        Lo        += throughput * payload.emissive;
        throughput *= payload.albedo;

        // ロシアンルーレット
        float p = max(throughput.r, max(throughput.g, throughput.b));
        if (RandomFloat(seed) > p) break;
        throughput /= p;

        // 次のレイ方向を BRDF サンプリング
        ray.Origin    = payload.worldPos + payload.normal * 0.001;
        ray.Direction = payload.sampleDir;
    }
    accumulationBuffer[DispatchRaysIndex().xy] += float4(Lo, 1.0);
}
```

---

### フェーズ 7-7: 空間・時間デノイジング

**実装項目**:
- Bilateral / Cross-Bilateral ブラー（法線・深度で重み付き）
- SVGF（Spatiotemporal Variance Guided Filter）の概念実装
- または NVIDIA NRD (NoisyRT Denoiser) SDK の統合

```hlsl
// A-trous ウェーブレットフィルタ（階層的空間ブラー）
[numthreads(8, 8, 1)]
void ATrousCS(uint3 dtID : SV_DispatchThreadID) {
    float3 centerNormal = gbufNormal[dtID.xy].xyz;
    float  centerDepth  = gbufDepth[dtID.xy].r;
    float4 sum          = float4(0, 0, 0, 0);
    float  weightSum    = 0;

    for (int y = -2; y <= 2; y++) {
        for (int x = -2; x <= 2; x++) {
            int2 offset   = int2(x, y) * stepSize; // stepSize = 1, 2, 4, 8, 16
            float3 N      = gbufNormal[dtID.xy + offset].xyz;
            float  d      = gbufDepth[dtID.xy + offset].r;
            float  wN     = pow(max(dot(centerNormal, N), 0.0), sigN);
            float  wD     = exp(-abs(centerDepth - d) / sigD);
            float  w      = wN * wD * h[abs(x)] * h[abs(y)]; // Gaussian kernel
            sum           += float4(noisyInput[dtID.xy + offset].rgb, 1.0) * w;
            weightSum     += w;
        }
    }
    filteredOutput[dtID.xy] = sum / weightSum;
}
```

---

### フェーズ 7-8: ハイブリッドレンダリング統合

**実装項目**:
- Rasterization（Deferred/Forward+）+ RT（AO・Shadow・Reflection）を統合
- RT パス: 低解像度（1/2 or 1/4）→ Upscale
- TAA で時間的ノイズ除去
- コスト制御（光源距離・重要度によるレイ本数可変）

```
統合パイプライン:
  [Geometry Pass (Raster)]
  [Shadow Map (Raster, CSM)]
  [RT Shadow (DXR, エリアライト or ソフトシャドウ)]
  [RTAO (DXR, 低解像度)]
  [RT Reflection (DXR, Rough=0.3以下のみ)]
  [Lighting Pass (Raster, PBR + IBL)]
  [SSR (Raster, スクリーン内の反射)]
  [Denoise (CS, SVGF)]
  [Composite (AO + Shadow + Reflection を合算)]
  [Post-Process (TAA, Bloom, Tone Mapping)]
```

---

## ファイル構成（完成時）

```
ray-tracing/
├── CMakeLists.txt
├── src/
│   ├── D3D12App.cpp/.h
│   ├── AccelerationStructure.cpp/.h  ← BLAS/TLAS 管理
│   ├── ShaderBindingTable.cpp/.h     ← SBT 構築
│   ├── RTPipeline.cpp/.h             ← DXR State Object
│   ├── RTAO.cpp/.h
│   ├── RTShadow.cpp/.h
│   ├── RTReflection.cpp/.h
│   ├── PathTracer.cpp/.h
│   └── Denoiser.cpp/.h
└── shaders/
    ├── rt_common.hlsli               ← ペイロード定義・共通関数
    ├── rtao.hlsl                     ← lib_6_3
    ├── rt_shadow.hlsl
    ├── rt_reflection.hlsl
    ├── path_tracer.hlsl
    └── atrous_denoise_cs.hlsl
```

---

## 確認チェックリスト

- [ ] TLAS 更新（動くオブジェクト）で描画結果が正しく変化する
- [ ] RTAO の結果が SSAO と比べて接触影が正確に出る
- [ ] RT Shadow でエリアライトのソフトシャドウが確認できる
- [ ] パストレーサーが十分なサンプル数でノイズのない画像を生成する
- [ ] デノイザーが少ないサンプル（1〜4 spp）でも実用的な品質を出す
- [ ] ハイブリッドモードで Raster との合成に破綻がない

---

## 関連ドキュメント
- [06-compute-effects.md](06-compute-effects.md) - 前フェーズ
- [08-character-rendering.md](08-character-rendering.md) - 次フェーズ
- [../concepts/14-advanced-rendering.md](../concepts/14-advanced-rendering.md)
