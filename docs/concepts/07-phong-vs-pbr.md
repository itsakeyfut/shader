# 07 - Phong vs PBR（経験則モデル vs 物理ベース）

## 経験則モデルの限界

### エネルギー非保存
Phong モデルは出射エネルギーが入射エネルギーを超えることがある。

```hlsl
// Phong: Diffuse + Specular を単純に加算
// Diffuse が明るい面 (NdotL ≈ 1) でも
// Specular を追加 → エネルギーが増える
float3 color = diffuse + specular; // 物理的に誤り
```

正しくは `diffuse + specular ≤ 入射光エネルギー` を保証する必要がある。

### 素材表現の不統一
| 問題 | Phong の課題 |
|---|---|
| 金属 | 金属特有のカラーハイライトを表現できない |
| 非金属 | フレネル効果（斜め方向の反射増大）が不正確 |
| パラメータ | `shininess`・`specularColor` は物理的意味がない |
| 一貫性 | アーティストごとに異なるパラメータ設定 → 素材の見た目がバラバラ |

---

## レンダリング方程式の基礎

James Kajiya（1986）が定式化した光輸送の基本式:

```
L_o(x, ω_o) = L_e(x, ω_o) + ∫_Ω f_r(x, ω_i, ω_o) L_i(x, ω_i) (n · ω_i) dω_i
```

- `L_o`: 出射輝度（カメラに届く光）
- `L_e`: 自発光（Emissive）
- `f_r`: BRDF（素材の反射特性）
- `L_i`: 入射輝度（光源からの光）
- `(n · ω_i)`: Lambert の余弦則

**リアルタイムでの近似**: 積分を限られた光源数の総和で近似し、
IBL で環境光を事前計算する。

---

## BRDF（Bidirectional Reflectance Distribution Function）

入射方向 `ω_i` から来た光が出射方向 `ω_o` にどれだけ反射するかを定義する関数。

```
f_r(ω_i, ω_o) = dL_o / (L_i × cos(θ_i) × dω_i)   [単位: 1/sr]
```

### BRDF の物理的制約
1. **非負性**: `f_r ≥ 0`
2. **相反性（Helmholtz）**: `f_r(ω_i, ω_o) = f_r(ω_o, ω_i)`
3. **エネルギー保存**: `∫_Ω f_r(ω_i, ω_o) cos(θ_o) dω_o ≤ 1`

### Phong BRDF としての表現
```
f_r_diffuse  = albedo / π               (Lambertian)
f_r_specular = ((n+2)/2π) × cos^n(θ_r) (Phong 鏡面）
```

エネルギー保存のためには `diffuse + specular ≤ 1` の制約が必要だが、
Phong はこれを保証しない。

---

## フレネル効果（Fresnel Effect）

**観察**: あらゆる素材は斜め方向（グレージング角）から見ると強く反射する。

```hlsl
// Schlick 近似（リアルタイムで最もよく使われる）
float3 F0 = ...;  // 垂直入射時の反射率（素材固有）
float  cosTheta = dot(H, V);
float3 fresnel  = F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
```

### F0（Fresnel 0°反射率）の値

| 素材 | F0 |
|---|---|
| 水 | 0.02 |
| プラスチック | 0.03〜0.05 |
| ガラス | 0.04 |
| 布 | 0.04〜0.05 |
| 銅 | (0.95, 0.64, 0.54) ← 色付き |
| 金 | (1.00, 0.71, 0.29) ← 色付き |
| 鉄 | (0.56, 0.57, 0.58) |

**金属（Conductor）**: F0 が 0.7〜1.0 で色を持つ
**非金属（Dielectric）**: F0 が 0.02〜0.07（単一値）

---

## なぜ PBR が現代レンダリングの標準か

### アーティスト観点
- **直感的なパラメータ**: Metalness（0〜1）/ Roughness（0〜1）
- **一貫した見た目**: 異なるライティング環境でも素材が破綻しない
- **物理に基づいた基準**: 光源の強度を物理単位（lux/cd/m²）で指定可能

### エンジン観点
- **スケーラビリティ**: 低精度（モバイル）から高精度（映画）まで同じパラメータ
- **IBL との統合**: 事前計算済み環境光（BRDF LUT、Irradiance Map）
- **将来性**: Ray Tracing への移行でもパラメータ再利用可能

### ゲームエンジンの採用
| エンジン | PBR 導入 |
|---|---|
| Unreal Engine 4 | 2014（Metalness/Roughness ワークフロー） |
| Unity | 2014（Standard Shader） |
| Frostbite | 2013（DICE SIGGRAPH） |
| id Tech 6 | 2016（DOOM） |

---

## Phong から PBR への移行戦略

### パラメータの対応

| Phong | PBR 相当 |
|---|---|
| Diffuse Color | Albedo（非金属: そのまま、金属: 0 に） |
| Specular Color | F0（非金属: 0.04、金属: Albedo） |
| Shininess | Roughness（`shininess = (1 - roughness)^n` で逆算） |
| - | Metalness（新規パラメータ） |

### HLSL 移行コード（中間ステップ）
```hlsl
// Phong パラメータを PBR 相当に変換
float3 albedo    = diffuseColor;
float  metalness = (specularColor.r > 0.5) ? 1.0 : 0.0; // 暫定変換
float  roughness = 1.0 - sqrt(shininess / 256.0);         // 近似変換
float3 F0        = lerp(float3(0.04, 0.04, 0.04), albedo, metalness);
```

---

## アーティスト向け素材パラメータの比較

### Phong ワークフロー
```
Diffuse Map:   RGB 色（直感的だが光の影響を含む場合あり）
Specular Map:  ハイライトの強さ・色（自由度が高すぎる）
Glossiness:    テクスチャまたは単一値
```

### PBR Metalness/Roughness ワークフロー（Unreal/Unity 標準）
```
Albedo:    線形色（光のない純粋な素材色）
Metalness: 0（非金属）or 1（金属）、中間値はトランジション用
Roughness: 0（鏡面）〜1（完全拡散）
AO:        ベイクされた環境光遮蔽（オプション）
```

### PBR Specular/Glossiness ワークフロー（Substance Designer 等）
```
Diffuse:   非金属の拡散色（金属部分は黒）
Specular:  反射色・強度（金属では明るい色）
Glossiness: 鏡面度（Roughness の逆）
```

---

## 移行判断フローチャート

```
素材が現実のものに基づくか？
  Yes → PBR 使用
  No（スタイライズド）→ Phong / Half-Lambert でも可

ターゲットプラットフォームは？
  PC / Console → PBR（フルクオリティ）
  Mobile / 旧世代 → Simplified PBR または Phong

チームにアーティストがいるか？
  Yes → PBR（テクスチャアセットが充実）
  No（個人開発）→ Phong から始めて徐々に PBR へ
```

---

## 関連ドキュメント
- [06-lighting-models.md](06-lighting-models.md) - Phong ライティング詳細
- [08-pbr-theory.md](08-pbr-theory.md) - PBR 数式の完全実装
- [09-ibl.md](09-ibl.md) - IBL（環境光の事前計算）
