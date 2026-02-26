# Roadmap: Phase 16 - Advanced Shadows（高度なシャドウ技術）

**プロジェクト**: `D:/dev/shader/advanced-shadows/`
**API**: Direct3D 12
**目標**: Phase 2 の基礎 Shadow Map を超え、AAA 品質のシャドウシステムを実装する

---

## 習得する概念

| 概念 | 関連ドキュメント |
|---|---|
| Shadow Map 基礎・PCF・VSM・CSM | [10-shadows.md](../concepts/10-shadows.md) |
| Ray Tracing (DXR) | [07-ray-tracing.md](07-ray-tracing.md) |
| Compute Shader | [15-compute-shaders.md](../concepts/15-compute-shaders.md) |
| Deferred Shading | [05-advanced-rendering.md](05-advanced-rendering.md) |

---

## フェーズ分け

### フェーズ 16-1: EVSM（Exponential Variance Shadow Maps）

VSM の Light Bleeding 問題をエクスポネンシャル変換で大幅に改善。

**実装項目**:
- Shadow Map に `exp(c × depth)` と `exp(-c × depth)` の 2 値を格納
- Chebyshev 上界をエクスポネンシャル空間で計算
- c の値をチューニング（c ≈ 40〜80）
- ブラーパス（Gaussian or Box Filter）でソフトシャドウ
- Texture2D フォーマット: `R32G32_FLOAT`（exp+ / exp-）

```hlsl
// EVSM Shadow Map 生成 PS
struct EVSMOut { float4 val : SV_Target; };

EVSMOut EVSMDepthPS(float depth : TEXCOORD0) {
    float c     = 40.0;
    float pos   = exp( c * depth);
    float neg   = exp(-c * depth);
    EVSMOut o;
    o.val = float4(pos, pos * pos, neg, neg * neg);
    return o;
}

// EVSM シャドウ評価
float EVSMShadow(float2 shadowUV, float receiverDepth, float c) {
    float4 moments = evsm.Sample(shadowSampler, shadowUV);

    float p1 = moments.r; // E[exp(c*d)]
    float p2 = moments.g; // E[exp(c*d)²]
    float p3 = moments.b; // E[exp(-c*d)]
    float p4 = moments.a; // E[exp(-c*d)²]

    float posBlend = ChebyshevUpperBound(
        float2(p1, p2), exp(c * receiverDepth), 0.001);
    float negBlend = ChebyshevUpperBound(
        float2(p3, p4), exp(-c * receiverDepth), 0.001);

    return min(posBlend, negBlend);
}

float ChebyshevUpperBound(float2 moments, float t, float minVariance) {
    float p        = (t <= moments.x) ? 1.0 : 0.0;
    float variance = moments.y - moments.x * moments.x;
    variance       = max(variance, minVariance);
    float d        = t - moments.x;
    float p_max    = variance / (variance + d * d);
    return max(p, p_max);
}
```

---

### フェーズ 16-2: Moment Shadow Maps（MSM）

EVSM を一般化した Moment-based 手法。4 次モーメントで精度と品質を向上。

**実装項目**:
- Shadow Map に 4 モーメント（`b1 = E[d], b2 = E[d²], b3 = E[d³], b4 = E[d⁴]`）を格納
- ハンケル行列式を使った上界計算
- `R16G16B16A16_FLOAT` フォーマット
- モーメントのブラー（可分フィルタ）

```hlsl
// Moment Shadow Map 生成
float4 MomentDepthPS(float depth : TEXCOORD0) : SV_Target {
    float  d  = depth;
    float  d2 = d  * d;
    float  d3 = d2 * d;
    float  d4 = d3 * d;
    return float4(d, d2, d3, d4);
}

// 4-moment shadow evaluation（Christoph Peters, 2015）
float ComputeMSMShadow(float4 b, float fragDepth) {
    // 数値安定化
    b[0] -= 0.035 * (b[1] - b[0]);
    // Cholesky 分解 + Hausdorff 距離で upper bound を計算
    // （実装は Peters 2015 論文のコードを参照）
    // ...
    return shadowFactor;
}
```

---

### フェーズ 16-3: Virtual Shadow Maps（VSM — UE5 方式）

UE5 5.0 で導入された高精度シャドウシステム。巨大な仮想シャドウアトラス（16K×16K）を
ページングで管理し、カメラ近辺だけ高精度なシャドウを実現する。

**実装項目**:
- 仮想テクスチャアトラス: 16384×16384 の深度アトラス（`DXGI_FORMAT_R32_FLOAT`）
- Page Table: 物理ページ番号を格納するテクスチャ
- Feedback: どのページが必要か判定する CS（HZB + カメラ近辺）
- Page Allocation: 必要ページをアトラスに割り当てる CPU/GPU ハイブリッド管理
- Nanite Compatible: Nanite メッシュのシャドウ描画（Virtual Geometry との統合）
- Invalidation: 動的オブジェクト移動時のページ無効化

```hlsl
// Page Table 参照（Page Table Lookup）
float SampleVirtualShadowMap(float3 worldPos, int lightIdx) {
    // Light Space 変換
    float4 lightClip = mul(lightViewProj[lightIdx], float4(worldPos, 1));
    float2 lightUV   = lightClip.xy / lightClip.w * 0.5 + 0.5;
    float  depth     = lightClip.z  / lightClip.w;

    // Mip レベル決定（カメラ距離に応じた Page サイズ）
    float  dist = length(worldPos - cameraPos);
    int    mip  = (int)log2(dist / nearPageDistance);
    mip         = clamp(mip, 0, MAX_MIP);

    // Page Table から物理ページ座標を取得
    float2 pageCoord = lightUV / exp2(mip);
    uint2  pageIdx   = (uint2)(pageCoord * PAGE_TABLE_SIZE);
    uint   pageData  = pageTable[lightIdx].Load(int3(pageIdx, mip));

    if (pageData == 0xFFFFFFFF) return 1.0; // ページ未割り当て → Lit
    uint2 physPage = UnpackPageData(pageData);

    // 物理アトラス上の UV を計算してサンプリング
    float2 physUV = (physPage + frac(pageCoord * PAGE_TABLE_SIZE)) / ATLAS_SIZE_IN_PAGES;
    float  shadowDepth = shadowAtlas.SampleLevel(pointSamp, physUV, 0).r;

    return depth < shadowDepth + SHADOW_BIAS ? 1.0 : 0.0;
}
```

---

### フェーズ 16-4: Contact Shadows（コンタクトシャドウ）

スクリーンスペースで微細な接触部分のシャドウを強化する。
通常のシャドウマップでは解像度不足で欠落しがちな足元・葉・細部を補完する。

**実装項目**:
- Screen Space Ray Marching（光源方向、短距離）
- 既存シャドウとのブレンド（通常シャドウ × コンタクトシャドウ）
- パラメータ: `MaxDistance (0.5m), NumSteps (16), Thickness (0.1m)`

```hlsl
float ComputeContactShadow(float3 worldPos, float3 L, float2 uv,
                            float maxDistance, int numSteps) {
    float3 rayStart = worldPos + L * 0.01; // オフセット（自己交差回避）
    float3 rayDir   = L;

    for (int i = 0; i < numSteps; i++) {
        float  t        = maxDistance * (i + 1.0) / numSteps;
        float3 samplePos = rayStart + rayDir * t;

        float4 clipPos   = mul(viewProj, float4(samplePos, 1));
        float2 sampleUV  = clipPos.xy / clipPos.w * float2(0.5, -0.5) + 0.5;

        if (any(sampleUV < 0) || any(sampleUV > 1)) break;

        float  sceneDepth = LinearizeDepth(depthTex.SampleLevel(p, sampleUV, 0).r);
        float  rayDepth   = LinearizeDepth(clipPos.z / clipPos.w);

        if (rayDepth > sceneDepth + 0.001 && rayDepth < sceneDepth + 0.1) {
            float fade = 1.0 - (float)i / numSteps; // 近いほど暗く
            return 1.0 - fade; // シャドウ係数
        }
    }
    return 1.0; // Lit
}
```

---

### フェーズ 16-5: Area Light Shadows（面光源シャドウ）

点光源近似ではなく面積を持つ光源の物理的に正確なソフトシャドウ。

**実装項目**:
- PCSS (Phase 2) の延長：ブロッカー平均距離から半影サイズを決定
- Poisson Disk のサイズを半影に応じてスケール
- Ray Traced Area Shadow（DXR）: 面光源上でランダムサンプル → 可視性テスト
- Soft Shadow の SVGF-like テンポラル蓄積（Phase 7 の Denoise を流用）

```hlsl
// PCSS Soft Shadow（改良版）
float PCSSAreaShadow(float2 shadowUV, float receiverDepth,
                     float lightSizeWorld, float lightDist) {
    // Step 1: Blocker Search
    float avgBlockerDepth = 0;
    int   numBlockers     = 0;
    float searchRadius    = lightSizeWorld / lightDist;

    for (int i = 0; i < BLOCKER_SAMPLES; i++) {
        float2 offset      = PoissonDisk[i] * searchRadius;
        float  shadowDepth = shadowMap.SampleLevel(p, shadowUV + offset, 0).r;
        if (shadowDepth < receiverDepth - BIAS) {
            avgBlockerDepth += shadowDepth;
            numBlockers++;
        }
    }
    if (numBlockers == 0) return 1.0;
    avgBlockerDepth /= numBlockers;

    // Step 2: Penumbra サイズ = ライトサイズ × (受光面深度 - ブロッカー深度) / ブロッカー深度
    float penumbraWidth = lightSizeWorld * (receiverDepth - avgBlockerDepth) / avgBlockerDepth;

    // Step 3: PCF with 可変カーネルサイズ
    float shadow = 0;
    for (int j = 0; j < PCF_SAMPLES; j++) {
        float2 offset = PoissonDisk[j] * penumbraWidth;
        shadow += shadowMap.SampleCmpLevelZero(shadowCompSampler,
                                                shadowUV + offset, receiverDepth - BIAS);
    }
    return shadow / PCF_SAMPLES;
}
```

---

### フェーズ 16-6: Shadow Atlas と動的ライト管理

多数の動的ライトのシャドウを 1 枚のアトラステクスチャで管理する。
ゲームエンジンの Shadow Manager 設計の核心。

**実装項目**:
- Shadow Atlas（例：4096×4096）へのタイル割り当てアルゴリズム（Bin Packing）
- ライトの重要度スコアリング（距離・サイズ・画面内存在）
- キャッシュ管理（静的オブジェクトのシャドウを再利用）
- フレームごとの優先度ソートと Dirty フラグ
- Point Light: Cube Shadow Map（6 面）をアトラス内に配置

```cpp
// Shadow Atlas Allocation
struct ShadowTile {
    int x, y, size;  // アトラス内座標
    int lightId;
    bool isDirty;
};

class ShadowAtlasManager {
    std::vector<ShadowTile> m_tiles;
    QuadTree m_freeList; // 空きスペース管理

    ShadowTile* Allocate(int lightId, int shadowMapSize) {
        auto* tile = m_freeList.FindBestFit(shadowMapSize);
        if (!tile) {
            // 重要度最低のタイルを解放して再割り当て
            EvictLeastImportant();
            tile = m_freeList.FindBestFit(shadowMapSize);
        }
        tile->lightId = lightId;
        tile->isDirty = true;
        return tile;
    }
};
```

---

## ファイル構成（完成時）

```
advanced-shadows/
├── CMakeLists.txt
├── src/
│   ├── ShadowAtlasManager.cpp/.h   ← アトラス割り当て
│   ├── EVSMPass.cpp/.h             ← EVSM 生成・ブラー
│   ├── MomentShadowPass.cpp/.h     ← MSM
│   ├── VirtualShadowMap.cpp/.h     ← VSM（UE5 方式）
│   ├── ContactShadow.cpp/.h        ← Screen Space
│   ├── AreaLightShadow.cpp/.h      ← PCSS / RT
│   └── ShadowComposite.cpp/.h      ← 統合・ブレンド
└── shaders/
    ├── evsm_depth_ps.hlsl
    ├── evsm_blur_cs.hlsl
    ├── evsm_shadow_ps.hlsl
    ├── moment_depth_ps.hlsl
    ├── moment_shadow_ps.hlsl
    ├── vsm_feedback_cs.hlsl
    ├── vsm_page_alloc_cs.hlsl
    ├── vsm_shadow_ps.hlsl
    ├── contact_shadow_cs.hlsl
    ├── pcss_area_shadow_ps.hlsl
    └── rt_area_shadow.hlsl         ← DXR
```

---

## 確認チェックリスト

- [ ] EVSM で VSM の Light Bleeding が改善される
- [ ] MSM で EVSM より滑らかなシャドウエッジが得られる
- [ ] Virtual Shadow Map でキャラクターの足元が高解像度シャドウになる
- [ ] Contact Shadow で接地面の微細な影が出る
- [ ] PCSS Area Shadow でライトサイズに応じた半影幅が変化する
- [ ] Shadow Atlas で 64 灯以上の動的ライトのシャドウが管理できる

---

## 関連ドキュメント
- [15-debug-rendering.md](15-debug-rendering.md) - 前フェーズ
- [17-mobile-optimization.md](17-mobile-optimization.md) - 次フェーズ
- [../concepts/10-shadows.md](../concepts/10-shadows.md)
- [07-ray-tracing.md](07-ray-tracing.md) - DXR Area Shadow 前提
