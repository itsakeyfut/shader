# Roadmap: Phase 3 - PBR

**プロジェクト**: `D:/dev/shader/pbr/`
**API**: Direct3D 11（IBL 事前計算は CS_5_0 or D3D12 移行）
**目標**: メタルネス/ラフネス PBR パイプラインと IBL の完全実装

---

## 習得する概念

| 概念 | 関連ドキュメント |
|---|---|
| Phong → PBR の理由 | [07-phong-vs-pbr.md](../concepts/07-phong-vs-pbr.md) |
| Cook-Torrance BRDF | [08-pbr-theory.md](../concepts/08-pbr-theory.md) |
| IBL（Irradiance・Pre-filtered・LUT） | [09-ibl.md](../concepts/09-ibl.md) |
| Compute Shader（IBL 生成用） | [15-compute-shaders.md](../concepts/15-compute-shaders.md) |

---

## フェーズ分け

### フェーズ 3-1: Cook-Torrance BRDF 数式の実装

**実装項目**:
- GGX NDF 関数 `D_GGX`
- Smith Geometry 関数 `G_Smith`
- Schlick Fresnel 関数 `F_Schlick`
- Cook-Torrance 鏡面 BRDF の組み合わせ
- エネルギー保存の確認（`kD = (1 - F) * (1 - metalness)`）

```hlsl
// 単一点光源でのテスト（IBL なし）
float3 CookTorranceBRDF(float3 N, float3 V, float3 L,
                        float3 albedo, float metalness, float roughness) {
    float3 H   = normalize(V + L);
    float NdotV = max(dot(N, V), 0.001);
    float NdotL = max(dot(N, L), 0.001);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    float3 F0  = lerp(float3(0.04, 0.04, 0.04), albedo, metalness);
    float  D   = D_GGX(NdotH, roughness);
    float  G   = G_Smith(NdotV, NdotL, roughness);
    float3 F   = F_Schlick(HdotV, F0);

    float3 kD      = (1.0 - F) * (1.0 - metalness);
    float3 specular = D * G * F / (4.0 * NdotV * NdotL + 0.0001);

    return (kD * albedo / PI + specular) * NdotL;
}
```

**確認方法**: 低 Roughness で鋭いハイライト、高 Roughness で広いハイライト。
Metal=1 でカラーハイライト（金・銅など）。

---

### フェーズ 3-2: PBR マテリアルパラメータ

**実装項目**:
- cbuffer で Albedo・Metalness・Roughness・AO をパラメータ化
- ImGui スライダーでリアルタイム調整（デバッグ用）
- 複数のオブジェクトに異なる素材設定

```hlsl
cbuffer MaterialData : register(b2) {
    float3 albedo;
    float  metalness;
    float  roughness;
    float  ao;
    float2 pad;
};
```

---

### フェーズ 3-3: テクスチャマップ読み込み（PBR テクスチャセット）

**実装項目**:
- Albedo (sRGB 読み込み)
- Normal Map（Linear、BC5 圧縮）
- ORM テクスチャ（Occlusion・Roughness・Metalness パック）
- stb_image または DirectXTex でのロード
- DXGI フォーマットの適切な選択（`_SRGB` vs `_UNORM`）

```hlsl
Texture2D albedoTex : register(t0); // sRGB
Texture2D normalTex : register(t1); // Linear
Texture2D ormTex    : register(t2); // Linear (ORM パック)

// ORM デコード
float3 orm      = ormTex.Sample(samp, uv).rgb;
float  ao       = orm.r;
float  roughness = orm.g;
float  metalness = orm.b;
```

---

### フェーズ 3-4: 点光源 / 指向性ライトとの統合

**実装項目**:
- Phase 2 の光源システムと PBR BRDF を統合
- 複数光源ループ（`[unroll]` または `[loop]`）
- 減衰関数の見直し（物理的な逆二乗則）

---

### フェーズ 3-5: Irradiance Map 生成（Compute Shader）

**実装項目**:
- HDR 環境マップ（Equirectangular）のロード（stb_image の HDR サポート）
- Equirectangular → Cube Map の変換 CS
- Irradiance Map の半球積分 CS（cos 重み付きサンプリング）

```hlsl
// irradiance_gen.hlsl
[numthreads(8, 8, 6)]
void GenerateIrradiance(uint3 id : SV_DispatchThreadID) {
    uint  face = id.z;
    float2 uv  = (float2(id.xy) + 0.5) / IRRADIANCE_SIZE;
    float3 N   = GetCubeDir(face, uv);

    // 半球積分
    float3 irradiance  = float3(0, 0, 0);
    // ... サンプリングループ
    irradianceOut[uint3(id.xy, face)] = float4(irradiance, 1.0);
}
```

**確認方法**: 拡散 IBL のみで柔らかい環境光が入る

---

### フェーズ 3-6: Pre-filtered Environment Map 生成

**実装項目**:
- 重点サンプリング（Hammersley + ImportanceSampleGGX）
- Roughness ≒ MipLevel の事前計算
- Cube Map ミップチェーンへの書き込み

---

### フェーズ 3-7: BRDF LUT 生成

**実装項目**:
- `IntegrateBRDF(NdotV, roughness)` 関数の実装
- 2D テクスチャへの出力（`DXGI_FORMAT_R16G16_FLOAT` 推奨）
- 1回だけ実行してキャッシュ（またはオフラインで生成）

---

### フェーズ 3-8: IBL との統合（Split-Sum）

**実装項目**:
- `F_SchlickRoughness` でラフネス考慮のフレネル
- Split-Sum の2項（Pre-filtered + BRDF LUT）を組み合わせ
- `kD * irradiance * albedo + specular_ibl`

```hlsl
// PS での最終合算
float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metalness);
float  NdotV = max(dot(N, V), 0.0);

// 鏡面 IBL
float3 R       = reflect(-V, N);
float3 kS      = F_SchlickRoughness(NdotV, F0, roughness);
float3 prefilt = prefilteredMap.SampleLevel(samp, R, roughness * MAX_LOD).rgb;
float2 brdf    = brdfLUT.Sample(samp, float2(NdotV, roughness)).rg;
float3 specIBL = prefilt * (F0 * brdf.x + brdf.y);

// 拡散 IBL
float3 kD = (1.0 - kS) * (1.0 - metalness);
float3 irradiance = irradianceMap.Sample(samp, N).rgb;
float3 diffIBL = kD * irradiance * albedo;

// 直接光 + IBL
float3 color = Lo + (diffIBL + specIBL) * ao;
```

**確認方法**: 環境光がラフネスで適切に変化。金属が周囲の色を反射する。

---

### フェーズ 3-9（発展）: Disney Principled BRDF の拡張パラメータ

**実装項目**:
- Subsurface（表面下散乱の近似）
- Sheen（布のグレージングハイライト）
- Clearcoat（二層コーティング）

---

## ファイル構成（完成時）

```
pbr/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── D3DApp.cpp/.h
│   ├── IBLGenerator.cpp/.h  ← IBL 事前計算
│   └── Scene.cpp/.h
└── shaders/
    ├── common.hlsli
    ├── brdf.hlsli           ← D_GGX, G_Smith, F_Schlick
    ├── ibl.hlsli            ← IBL 計算共通
    ├── pbr_vs.hlsl
    ├── pbr_ps.hlsl
    ├── irradiance_gen.hlsl  ← CS
    ├── prefilter_gen.hlsl   ← CS
    └── brdf_lut_gen.hlsl    ← CS
```

---

## 確認チェックリスト

- [ ] GGX ハイライトが Roughness に応じて正しく変化する
- [ ] Metalness=1 でカラーハイライト（F0 = Albedo 色）が確認できる
- [ ] Irradiance Map が環境の色を正確に反映している
- [ ] BRDF LUT テクスチャを外部ビューアで確認した（白→黒のグラデーション）
- [ ] Split-Sum でエネルギー保存が概ね成立している（真っ白なシーンで過度に明るくならない）

---

## 関連ドキュメント
- [02-lighting.md](02-lighting.md) - 前フェーズ
- [../concepts/08-pbr-theory.md](../concepts/08-pbr-theory.md)
- [../concepts/09-ibl.md](../concepts/09-ibl.md)
- [04-post-process.md](04-post-process.md) - 次フェーズ
