# 04 - Normal Handling（法線の扱い）

## なぜ法線は座標と同じ変換ができないか

頂点座標は Model Matrix `M` で変換される:
```
worldPos = M × objPos
```

法線に同じ行列を適用すると**間違い**になる。

### 問題の例
```
非一様スケール（X方向のみ2倍）した場合:
M = Scale(2, 1, 1)

元の法線: n = (1, 1, 0) → 正規化すると (0.707, 0.707, 0)
M × n = (2, 1, 0) → 正規化すると (0.894, 0.447, 0) ← サーフェスに垂直でなくなる！
```

**問題の本質**: 非一様スケールや剪断（shear）変換では、法線の直交性が崩れる。

### 正しい法線変換行列
法線は **M の逆転置行列** `(M⁻¹)ᵀ` で変換する:

```
worldNormal = (M⁻¹)ᵀ × objNormal
```

**証明の直感**:
- 接線ベクトル `t` は `M` で変換される: `t' = M × t`
- 法線 `n` は接線に垂直: `n · t = 0`
- `n'` も `t'` に垂直でなければならない: `n' · (M × t) = 0`
- これを満たすのが `n' = (M⁻¹)ᵀ × n`

### 一様スケール・回転のみの場合
一様スケール `s` と回転 `R` のみなら:
```
(M⁻¹)ᵀ = (1/s) × R
```
`normalize()` するので係数は不要 → **法線行列 = 回転行列** で OK

```hlsl
// 一般的な最適化（一様スケール + 回転のみのモデル行列）
float3 worldNormal = normalize(mul((float3x3)modelMatrix, input.normal));

// 非一様スケールがある場合は CPU 側で逆転置を計算して渡す
cbuffer PerObject : register(b0) {
    float4x4 modelMatrix;
    float4x4 normalMatrix; // (M^-1)^T を CPU で計算済み
};
float3 worldNormal = normalize(mul((float3x3)normalMatrix, input.normal));
```

---

## Tangent / Bitangent / Normal の関係（TBN 行列）

サーフェスの局所座標系を定義する3つの正規直交ベクトル:

```
T (Tangent)   - U 方向の接線
B (Bitangent) - V 方向の接線（TBN 系では Binormal とも）
N (Normal)    - 面法線
```

```
Y (Up)
↑   ↗ T (Tangent)
|  /
| /
|/____→ B (Bitangent)
      N (Normal = T × B、画面手前方向)
```

### TBN 行列の役割
ノーマルマップはタンジェント空間で格納されている（青みがかった画像）。
TBN 行列を使ってタンジェント空間 → ワールド空間に変換する:

```hlsl
// ワールド空間の TBN ベクトルを構築
float3 T = normalize(mul((float3x3)modelMatrix, input.tangent.xyz));
float3 N = normalize(mul((float3x3)normalMatrix, input.normal));
// Gram-Schmidt 再直交化（補間で歪みが生じるため）
T = normalize(T - dot(T, N) * N);
float3 B = cross(N, T) * input.tangent.w; // w = 1 or -1（ミラーUV対応）

// タンジェント空間 → ワールド空間への変換行列
float3x3 TBN = float3x3(T, B, N); // 各行がワールド空間のベクトル

// ノーマルマップサンプリング → タンジェント空間法線
float3 tangentNormal = normalMap.Sample(sampler, uv).rgb * 2.0 - 1.0;
// [0,1] → [-1,1] に変換

// ワールド空間に変換
float3 worldNormal = normalize(mul(tangentNormal, TBN));
// または: mul(transpose(TBN), tangentNormal) — 正規直交行列は転置=逆行列
```

---

## Tangent Space vs World Space 法線マップ

### Tangent Space 法線マップ（最一般的）
- テクスチャの色: 大半が青みがかり（Z=1 方向が多い）
- **利点**: UV タイリング可能、メッシュ変形に追従
- **欠点**: TBN 行列の計算が必要
- **用途**: キャラクター・プロップなど変形するものすべて

### World Space 法線マップ
- テクスチャの色: 法線の XYZ → RGB に直接対応
- **利点**: 単純な変換（モデルを動かさない前提）
- **欠点**: UV タイリング不可、メッシュ回転に非対応
- **用途**: 地形（Terrain）など固定ジオメトリ

### Object Space 法線マップ
- モデル空間の法線を格納
- キャラクターアニメーションを考慮しないバインドポーズ向け

---

## Mikkel T. Mikkelsen の TBN 生成アルゴリズム（MikkTSpace）

業界標準の正規直交タンジェント空間生成アルゴリズム。
Blender・Maya・Substance Painter 等がこれを採用。

### 基本手順（簡略版）
```cpp
// CPU 側（メッシュロード時に計算）
for (各三角形 (v0, v1, v2)):
    float3 edge1 = v1.pos - v0.pos;
    float3 edge2 = v2.pos - v0.pos;
    float2 duv1  = v1.uv  - v0.uv;
    float2 duv2  = v2.uv  - v0.uv;

    float f = 1.0f / (duv1.x * duv2.y - duv2.x * duv1.y);

    float3 tangent;
    tangent.x = f * (duv2.y * edge1.x - duv1.y * edge2.x);
    tangent.y = f * (duv2.y * edge1.y - duv1.y * edge2.y);
    tangent.z = f * (duv2.y * edge1.z - duv1.y * edge2.z);

    // 各頂点にタンジェントを累積（後で正規化・平均化）
    v0.tangent += tangent;
    v1.tangent += tangent;
    v2.tangent += tangent;

// 頂点単位で Gram-Schmidt 直交化
for (各頂点 v):
    float3 n = v.normal;
    float3 t = normalize(v.tangent - dot(v.tangent, n) * n);
    float3 b = cross(n, t);
    // 鏡面UV（ミラーモデル）の向きを w に格納
    v.tangent.w = (dot(cross(n, t), v.bitangent) < 0.0f) ? -1.0f : 1.0f;
```

---

## タンジェント空間での光源計算

ワールド空間ではなく、タンジェント空間で全計算を行う方法:
（PS への転送データを減らせる）

```hlsl
// VS でライトベクトル・視線ベクトルをタンジェント空間に変換
VSOutput VSMain(VSInput input) {
    // ... TBN 構築 ...

    float3x3 TBNinv = transpose(TBN); // 正規直交なので転置=逆

    float3 lightDir = lightPos - worldPos;
    output.lightDirTS = mul(TBNinv, lightDir); // タンジェント空間へ

    float3 viewDir = cameraPos - worldPos;
    output.viewDirTS = mul(TBNinv, viewDir);

    return output;
}

// PS ではノーマルマップを直接使えばよい
float4 PSMain(VSOutput input) : SV_Target {
    float3 N = normalMap.Sample(samp, input.uv).rgb * 2.0 - 1.0;
    float3 L = normalize(input.lightDirTS);

    float diffuse = max(dot(N, L), 0.0);
    // ...
}
```

---

## よくある問題と対策

| 問題 | 原因 | 対策 |
|---|---|---|
| 法線が歪む | 非一様スケールに M を使用 | 法線行列 `(M⁻¹)ᵀ` を使用 |
| 法線マップが青すぎる | タンジェント計算ミス | CPU 側タンジェント生成を確認 |
| 継ぎ目が目立つ | UV シームでのタンジェント不一致 | MikkTSpace に準拠したツール使用 |
| 法線が反転 | 鏡面 UV で tangent.w を無視 | `cross(N, T) * tangent.w` で符号を適用 |
| ミラーモデルで縦線 | bitangent の向きが反転 | tangent.w（±1）を格納し GPU で適用 |

---

## 関連ドキュメント
- [02-coordinate-spaces.md](02-coordinate-spaces.md) - 座標空間の概要
- [03-vertex-transformation.md](03-vertex-transformation.md) - MVP 変換
- [05-texturing-sampling.md](05-texturing-sampling.md) - ノーマルマップテクスチャ
- [06-lighting-models.md](06-lighting-models.md) - ライティングでの法線使用
