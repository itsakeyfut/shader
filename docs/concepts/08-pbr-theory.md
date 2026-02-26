# 08 - PBR Theory（物理ベースレンダリング理論）

## マイクロファセット理論

現実のサーフェスは、微細なスケールで見ると小さな鏡（マイクロファセット）の集合体。

```
粗いサーフェス（高 Roughness）:
  ↗↘↗↗↘↗  ← ランダムな向きの微細な凹凸
  光が各方向に散乱 → 広いハイライト

滑らかなサーフェス（低 Roughness）:
  → → → →  ← ほぼ均一な向き
  光がほぼ同方向に反射 → 鋭いハイライト
```

マイクロファセット BRDF の一般式:
```
f_r(ω_i, ω_o) = [D(h) × G(ω_i, ω_o) × F(ω_o, h)] / [4 × (n·ω_i) × (n·ω_o)]
```

- `D(h)`: NDF（法線分布関数）— ハーフベクトル方向のマイクロファセット密度
- `G(ω_i, ω_o)`: Geometry 関数 — 自己遮蔽（シャドウイング・マスキング）
- `F(ω_o, h)`: Fresnel 関数 — 角度による反射率変化
- `4 × (n·ω_i) × (n·ω_o)`: 正規化項

---

## Cook-Torrance BRDF 完全実装

```hlsl
// 完全な Cook-Torrance BRDF
float3 CookTorranceBRDF(
    float3 N,        // 法線
    float3 V,        // 視線方向
    float3 L,        // ライト方向
    float3 albedo,
    float  metalness,
    float  roughness
) {
    float3 H = normalize(V + L);
    float NdotV = max(dot(N, V), 0.001);
    float NdotL = max(dot(N, L), 0.001);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    // F0: 垂直入射時の反射率
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metalness);

    // D: NDF (GGX)
    float D = D_GGX(NdotH, roughness);

    // G: Geometry (Smith's Schlick-GGX)
    float G = G_Smith(NdotV, NdotL, roughness);

    // F: Fresnel (Schlick)
    float3 F = F_Schlick(HdotV, F0);

    // 鏡面反射項
    float3 specular = (D * G * F) / (4.0 * NdotV * NdotL + 0.0001);

    // エネルギー保存: 鏡面 + 拡散 ≤ 1
    float3 kS = F;               // 鏡面反射率
    float3 kD = (1.0 - kS) * (1.0 - metalness); // 拡散率（金属はゼロ）

    // Lambertian 拡散
    float3 diffuse = kD * albedo / PI;

    return (diffuse + specular) * NdotL;
}
```

---

## NDF: GGX / Trowbridge-Reitz

法線分布関数（Normal Distribution Function）。
どれだけのマイクロファセットがハーフベクトル `H` の方向を向いているか。

```hlsl
float D_GGX(float NdotH, float roughness) {
    float a  = roughness * roughness;  // α = roughness^2（知覚的に線形）
    float a2 = a * a;

    float denom = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    denom = PI * denom * denom;

    return a2 / max(denom, 0.0001);
}
```

### なぜ GGX が標準か
- より長いテール（Long-tail）→ エネルギーを長距離まで散乱させる
- 現実のハイライト形状に近い（Phong のような急激な落ち込みがない）
- Beckmann より直感的なパラメータ

### roughness^2 の意味
```
知覚的に線形な roughness:
  roughness = 0.5 → α = 0.25
  これにより UI の roughness スライダーが線形に見える
```

---

## Geometry Function: Smith's Schlick-GGX

自己遮蔽（Shadowing-Masking）関数。
マイクロファセットが隣のファセットに隠されてしまう確率を計算。

```hlsl
// Schlick-GGX の G1（1方向分）
float G_SchlickGGX(float NdotX, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;  // 直接ライティング用

    // IBL 用は: k = roughness^2 / 2

    return NdotX / (NdotX * (1.0 - k) + k);
}

// Smith: 視線方向と光源方向の両方を考慮
float G_Smith(float NdotV, float NdotL, float roughness) {
    float ggxV = G_SchlickGGX(NdotV, roughness);
    float ggxL = G_SchlickGGX(NdotL, roughness);
    return ggxV * ggxL;
}
```

---

## Fresnel: Schlick 近似

```hlsl
float3 F_Schlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Roughness を考慮した拡張版（IBL 用）
float3 F_SchlickRoughness(float cosTheta, float3 F0, float roughness) {
    return F0 + (max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0) - F0)
               * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}
```

---

## メタルネス / ラフネスワークフロー

### アルベドとF0の関係
```hlsl
// 非金属（Dielectric）: albedo = 拡散色、F0 = 0.04（固定）
// 金属（Conductor）:  albedo = 0（拡散なし）、F0 = albedo（テクスチャから）

float3 F0     = lerp(float3(0.04, 0.04, 0.04), albedo, metalness);
float3 kD_albedo = albedo * (1.0 - metalness); // 金属の拡散はゼロ
```

### 素材パラメータの正しい値
```
非金属（プラスチック、布、石など）:
  Metalness: 0.0
  F0: 0.04（固定、UV 変化なし）
  Roughness: 素材に応じて 0.1〜1.0

金属（鉄、銅、金など）:
  Metalness: 1.0
  F0: Albedo テクスチャの値（色付き反射）
  Roughness: 磨き具合で 0.0〜0.6 程度

注意: Metalness の中間値（0〜1）は素材の混合（汚れた金属等）に使う
```

---

## エネルギー保存の実装

```hlsl
// 鏡面フレネル = kS（反射される割合）
float3 kS = F;

// 拡散は残りのエネルギー（フレネルで反射されなかった分）
float3 kD = float3(1.0, 1.0, 1.0) - kS;

// 金属には拡散なし（電子が光を吸収して別の波長で再放射）
kD *= (1.0 - metalness);

// 最終色
float3 Lo = (kD * albedo / PI + specular) * radiance * NdotL;
```

---

## Disney Principled BRDF（発展）

Burley（2012）が提案。アーティスト向けに直感的な多数パラメータを持つ拡張 BRDF。

| パラメータ | 範囲 | 説明 |
|---|---|---|
| `baseColor` | RGB | 素材の基本色 |
| `metallic` | 0〜1 | 金属度 |
| `roughness` | 0〜1 | 粗さ |
| `subsurface` | 0〜1 | 表面下散乱（肌・ろうそく等） |
| `specular` | 0〜1 | F0 のスケール（0.5 ≈ F0=0.04） |
| `specularTint` | 0〜1 | ハイライトをベースカラーに染める |
| `anisotropic` | 0〜1 | 異方性（ヘアー・CD・ブラッシュドメタル） |
| `sheen` | 0〜1 | 布のグレージングハイライト |
| `sheenTint` | 0〜1 | シーンカラーのティント |
| `clearcoat` | 0〜1 | クリアコート層（車のペイント等） |
| `clearcoatGloss` | 0〜1 | クリアコートの光沢 |

---

## 完全なライティングループ実装

```hlsl
float3 Lo = float3(0.0, 0.0, 0.0);

// 各点光源からの寄与
[unroll]
for (int i = 0; i < NUM_LIGHTS; i++) {
    float3 L        = normalize(lights[i].pos - worldPos);
    float  dist     = length(lights[i].pos - worldPos);
    float  atten    = 1.0 / (dist * dist);
    float3 radiance = lights[i].color * atten;

    Lo += CookTorranceBRDF(N, V, L, albedo, metalness, roughness) * radiance;
}

// IBL（環境光）を加算
float3 ambient = CalcIBL(N, V, albedo, metalness, roughness, ao);

float3 finalColor = Lo + ambient;
```

---

## 関連ドキュメント
- [07-phong-vs-pbr.md](07-phong-vs-pbr.md) - Phong からの移行理由
- [09-ibl.md](09-ibl.md) - IBL の事前計算と統合
- [../roadmap/03-pbr.md](../roadmap/03-pbr.md) - PBR 実装フェーズ
