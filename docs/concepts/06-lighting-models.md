# 06 - Lighting Models（ライティングモデル）

## 光の物理的基礎

### 輻射量（Radiometric Quantities）
| 量 | 記号 | 単位 | 意味 |
|---|---|---|---|
| 放射フラックス | Φ | W（ワット） | 単位時間の放射エネルギー |
| 照度（Irradiance） | E | W/m² | 面積あたりの受光量 |
| 放射輝度（Radiance） | L | W/(m²·sr) | 方向あたりの輝度 |

実装では主に **Radiance**（L）を扱う。カメラに届く光の量。

### Lambert の余弦則
```
E = L_i × cos(θ) = L_i × max(dot(N, L), 0)
```
- `N`: 面法線（正規化済み）
- `L`: 光源方向（サーフェスから光源へ、正規化済み）
- `θ`: 光源方向と法線のなす角

---

## Ambient / Diffuse / Specular 分解

Phong モデルは光を3成分に分解する:

```
Final = Ambient + Diffuse + Specular
```

```hlsl
// 簡易実装
float3 ambient  = ambientColor * albedo;
float3 diffuse  = lightColor * albedo * max(dot(N, L), 0.0);
float3 specular = lightColor * specColor * pow(max(dot(R, V), 0.0), shininess);
float3 color    = ambient + diffuse + specular;
```

**注意**: 3成分モデルは物理的には近似。PBR で置き換えられる。

---

## Lambertian Diffuse（拡散反射）

最もシンプルな拡散モデル。方向に依存しない（等方的）。

```hlsl
float NdotL    = max(dot(N, L), 0.0);
float3 diffuse = lightColor * albedo * NdotL;
```

**物理的意味**: 光がサーフェスに入射し、内部で散乱して全方向に均等に出射する。
マット素材（石膏・粗い布）の近似として有効。

---

## Phong Specular

反射ベクトル `R` と視線ベクトル `V` のなす角から鏡面ハイライトを計算。

```hlsl
float3 R = reflect(-L, N);          // L を N 周りに反射
float3 V = normalize(cameraPos - worldPos);
float spec = pow(max(dot(R, V), 0.0), shininess);
float3 specular = lightColor * specColor * spec;
```

**問題点**: `dot(R, V) < 0`（後ろから見る角度）での pow の計算で問題が起きやすい。
`max(0, ...)` でクランプする。

---

## Blinn-Phong 最適化

Phong の `reflect` 計算を **ハーフベクトル** `H` で置き換え。

```hlsl
float3 H = normalize(L + V);        // ハーフベクトル
float spec = pow(max(dot(N, H), 0.0), shininess);
```

**利点**:
- `reflect` より安価
- 大きい `shininess` でも自然なハイライト形状
- 物理的にわずかに正確（エネルギー保存に近い）

### Shininess（光沢度）のガイドライン
| 素材 | Shininess |
|---|---|
| 粗い石 | 1〜10 |
| 木材 | 20〜50 |
| プラスチック | 50〜100 |
| 磨かれた金属 | 200〜1000 |

---

## Half-Lambert（Valve 技法）

Valve が『Half-Life』で導入した改良 Diffuse。
暗い面も情報量を保持し、アーティスティックなコントロールが効く。

```hlsl
// 通常の Lambert
float NdotL = max(dot(N, L), 0.0);

// Half-Lambert（Valve）
float halfLambert = dot(N, L) * 0.5 + 0.5; // [-1,1] → [0,1]
halfLambert = halfLambert * halfLambert;      // ガンマ補正的な強調（オプション）
```

**効果**: 影側（N·L < 0）の面が真っ黒にならず、ディテールが見える。
トゥーンシェーダーや NPR レンダリングでよく使われる。

---

## 光源種別

### Directional Light（指向性ライト）
- 太陽などの無限遠光源
- 光の方向は全域で一定、距離減衰なし

```hlsl
float3 L = normalize(-lightDirection); // ライト方向の逆（サーフェスから光源へ）
float3 lightColor = directionalColor;  // 減衰なし
```

### Point Light（点光源）
- 電球などの全方向光源
- 距離に応じて減衰

```hlsl
float3 toLight = lightPos - worldPos;
float  dist    = length(toLight);
float3 L       = normalize(toLight);
float  atten   = 1.0 / (dist * dist); // 物理的な逆二乗則
float3 radiance = lightColor * atten;
```

### Spot Light（スポットライト）
- 特定方向・角度範囲に絞った光源

```hlsl
float3 L        = normalize(lightPos - worldPos);
float  theta    = dot(L, normalize(-lightDir));  // cos(angle)
float  inner    = cos(innerConeAngle);
float  outer    = cos(outerConeAngle);
float  epsilon  = inner - outer;
float  spotFactor = clamp((theta - outer) / epsilon, 0.0, 1.0);
float3 radiance = lightColor * spotFactor / (dist * dist);
```

### Area Light（面光源）
- LTC（Linearly Transformed Cosines）など高度な近似が必要
- PBR フェーズで学習

---

## 減衰関数（Attenuation）

### 物理的な逆二乗則
```hlsl
float attenuation = 1.0 / (dist * dist);
```

### 定数・線形・二次の組み合わせ（レガシー）
```hlsl
float attenuation = 1.0 / (constant + linear * dist + quadratic * dist * dist);
```

### 範囲付き平滑減衰（ゲームでよく使われる）
```hlsl
float range = lightRadius;
float atten = clamp(1.0 - (dist / range), 0.0, 1.0);
atten = atten * atten; // 滑らかな落ち込み
```

### Unreal Engine 式（物理的 + 切り落とし）
```hlsl
float atten = pow(clamp(1.0 - pow(dist / range, 4), 0, 1), 2) / (dist * dist + 1);
```

---

## 完全なライティング実装例（Blinn-Phong）

```hlsl
struct LightData {
    float3 position;
    float  range;
    float3 color;
    float  intensity;
};

float3 CalcBlinnPhong(
    float3 worldPos,
    float3 N,
    float3 albedo,
    float3 specColor,
    float  shininess,
    LightData light,
    float3 cameraPos
) {
    float3 toLight = light.position - worldPos;
    float  dist    = length(toLight);
    float3 L       = normalize(toLight);
    float3 V       = normalize(cameraPos - worldPos);
    float3 H       = normalize(L + V);

    // 減衰
    float atten = clamp(1.0 - dist / light.range, 0.0, 1.0);
    atten *= atten;

    float3 radiance = light.color * light.intensity * atten;

    // Diffuse (Lambertian)
    float NdotL   = max(dot(N, L), 0.0);
    float3 diffuse = radiance * albedo * NdotL;

    // Specular (Blinn-Phong)
    float NdotH    = max(dot(N, H), 0.0);
    float spec     = pow(NdotH, shininess);
    float3 specular = radiance * specColor * spec;

    return diffuse + specular;
}
```

---

## 関連ドキュメント
- [04-normal-handling.md](04-normal-handling.md) - 法線変換（TBN）
- [07-phong-vs-pbr.md](07-phong-vs-pbr.md) - PBR への移行理由
- [10-shadows.md](10-shadows.md) - 影の実装
- [../roadmap/02-lighting.md](../roadmap/02-lighting.md) - ライティングプロジェクト
