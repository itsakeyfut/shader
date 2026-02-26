# 17 - Procedural Noise（プロシージャルノイズ）

## Value Noise / Gradient Noise

### Value Noise（値ノイズ）
グリッド点にランダムな値を割り当て、補間する。

```hlsl
float Hash(float2 p) {
    p = frac(p * float2(0.1031, 0.1030));
    p += dot(p, p.yx + 33.33);
    return frac((p.x + p.y) * p.x);
}

float ValueNoise(float2 p) {
    float2 i = floor(p);
    float2 f = frac(p);

    // Hermite 補間（f² × (3 - 2f)）
    float2 u = f * f * (3.0 - 2.0 * f);

    // 4コーナーの値を補間
    return lerp(
        lerp(Hash(i + float2(0,0)), Hash(i + float2(1,0)), u.x),
        lerp(Hash(i + float2(0,1)), Hash(i + float2(1,1)), u.x),
        u.y
    );
}
```

---

## Perlin Noise（勾配ノイズ）

Ken Perlin（1983）による古典的なグラデーションノイズ。
Value Noise より自然な見た目。

```hlsl
float2 GradientHash(float2 p) {
    // 擬似乱数の方向ベクトル
    p = float2(dot(p, float2(127.1, 311.7)), dot(p, float2(269.5, 183.3)));
    return -1.0 + 2.0 * frac(sin(p) * 43758.5453);
}

float PerlinNoise(float2 p) {
    float2 i = floor(p);
    float2 f = frac(p);

    // Quintic 補間（より滑らか）
    float2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);

    // 4コーナーの勾配と内積
    float a = dot(GradientHash(i + float2(0,0)), f - float2(0,0));
    float b = dot(GradientHash(i + float2(1,0)), f - float2(1,0));
    float c = dot(GradientHash(i + float2(0,1)), f - float2(0,1));
    float d = dot(GradientHash(i + float2(1,1)), f - float2(1,1));

    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}
```

**出力範囲**: 約 [-0.707, 0.707]（2D）→ 使用前に [0,1] に変換:
```hlsl
float value = PerlinNoise(uv) * 0.5 + 0.5;
```

---

## Simplex Noise

Perlin の改良版（Ken Perlin, 2001）。
2D: O(n²) → 三角形格子ベースで計算量減少、アーティファクト削減。

```hlsl
// Simplex 2D（簡略版）
float SimplexNoise(float2 p) {
    const float F2 = 0.366025; // (sqrt(3)-1)/2
    const float G2 = 0.211324; // (3-sqrt(3))/6

    float2 i  = floor(p + dot(p, float2(F2, F2)));
    float2 x0 = p - i + dot(i, float2(G2, G2));

    float2 i1 = (x0.x > x0.y) ? float2(1, 0) : float2(0, 1);
    float2 x1 = x0 - i1 + G2;
    float2 x2 = x0 - 1.0 + 2.0 * G2;

    // 3つの寄与を合算
    float3 t = max(0.5 - float3(dot(x0,x0), dot(x1,x1), dot(x2,x2)), 0.0);
    t = t * t * t * t;

    float3 grad;
    grad.x = dot(GradientHash(i),    x0);
    grad.y = dot(GradientHash(i+i1), x1);
    grad.z = dot(GradientHash(i+1),  x2);

    return 70.0 * dot(t, grad);
}
```

---

## Worley / Voronoi Noise

F1: 最近傍の特徴点距離。有機的・細胞状のパターン。

```hlsl
float WorleyNoise(float2 p) {
    float2 i    = floor(p);
    float2 f    = frac(p);
    float  minD = 1e10;

    // 近傍 3×3 セルを探索
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            float2 neighbor = float2(x, y);
            // セル内のランダム特徴点
            float2 point = neighbor + float2(Hash(i + neighbor), Hash(i + neighbor + 0.5));
            float  d     = length(f - point);
            minD         = min(minD, d);
        }
    }
    return minD;
}

// F1: 最近傍（細胞の境界）
// F2 - F1: セル内の相対位置（クラック状）
// F1 × F2: 境界がより強調される
```

**用途**: 岩石・生体組織・錆・木の節

---

## FBM（Fractional Brownian Motion）

ノイズを複数の周波数（オクターブ）で重ね合わせて詳細を追加。

```hlsl
float FBM(float2 p, int octaves = 8) {
    float value     = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;

    for (int i = 0; i < octaves; i++) {
        value     += amplitude * PerlinNoise(p * frequency);
        amplitude *= 0.5;    // 高周波ほど振幅が小さい
        frequency *= 2.0;    // 各オクターブで周波数倍
        // またはランダムな回転を加えると異方性が減る
        p = float2x2(1.6, 1.2, -1.2, 1.6) * p; // 回転+スケール
    }
    return value;
}
```

**用途**: 地形高さマップ、雲、炎、水面

---

## SDF（Signed Distance Fields）

点からサーフェスまでの符号付き距離を返す関数。
内部: 負、外部: 正、境界: 0。

```hlsl
// 基本 SDF プリミティブ
float SdfSphere(float3 p, float r) {
    return length(p) - r;
}

float SdfBox(float3 p, float3 b) {
    float3 d = abs(p) - b;
    return length(max(d, 0.0)) + min(max(d.x, max(d.y, d.z)), 0.0);
}

float SdfCapsule(float3 p, float3 a, float3 b, float r) {
    float3 ab = b - a, ap = p - a;
    float  t  = clamp(dot(ap, ab) / dot(ab, ab), 0.0, 1.0);
    return length(ap - t * ab) - r;
}

// ブーリアン演算
float SdfUnion(float a, float b)        { return min(a, b); }
float SdfIntersect(float a, float b)    { return max(a, b); }
float SdfSubtract(float a, float b)     { return max(a, -b); }

// スムーズ結合（Smooth Union）
float SdfSmoothUnion(float a, float b, float k) {
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return lerp(b, a, h) - k * h * (1.0 - h);
}
```

---

## Ray Marching

SDF を使って光線とシーンの交差を反復計算で求める。

```hlsl
// シーンの SDF（複合体）
float SceneSDF(float3 p) {
    float d = SdfSphere(p - float3(0, 0, 5), 1.0);
    d = SdfUnion(d, SdfBox(p - float3(0, -1, 5), float3(5, 0.1, 5)));
    return d;
}

// Ray Marching メインループ
float4 RayMarchPS(float2 uv : TEXCOORD0) : SV_Target {
    float2 ndc = uv * 2.0 - 1.0;
    float3 ro  = cameraPos;
    float3 rd  = normalize(float3(ndc.x, ndc.y, -1.0)); // 簡易カメラ

    float t     = 0.0;
    float tMax  = 100.0;
    int   steps = 0;

    [loop]
    for (int i = 0; i < 128; i++) {
        float3 p = ro + rd * t;
        float  d = SceneSDF(p);

        if (d < 0.001) {
            // ヒット! 法線を推定してシェーディング
            float3 N = CalcNormal(p);
            float3 color = Shade(p, N, rd);
            return float4(color, 1.0);
        }

        t += d; // 符号付き距離分だけ安全に前進
        if (t > tMax) break;
        steps = i;
    }

    // スカイカラー
    return float4(0.2, 0.5, 0.8, 1.0);
}

// 数値微分で法線を計算
float3 CalcNormal(float3 p) {
    float2 e = float2(0.001, 0.0);
    return normalize(float3(
        SceneSDF(p + e.xyy) - SceneSDF(p - e.xyy),
        SceneSDF(p + e.yxy) - SceneSDF(p - e.yxy),
        SceneSDF(p + e.yyx) - SceneSDF(p - e.yyx)
    ));
}
```

---

## ドメインワーピング（Domain Warping）

ノイズの入力座標をノイズで歪ませる。有機的・複雑なパターン生成。

```hlsl
// Inigo Quilez の手法
float3 DomainWarp(float2 p) {
    // 第1層: オフセットを計算
    float2 warp1 = float2(
        FBM(p + float2(1.7, 9.2)),
        FBM(p + float2(8.3, 2.8))
    );

    // 第2層: さらにワープ
    float2 warp2 = float2(
        FBM(p + 4.0 * warp1 + float2(1.7, 9.2)),
        FBM(p + 4.0 * warp1 + float2(8.3, 2.8))
    );

    return FBM(p + 4.0 * warp2);
}
```

---

## 実用的な用途まとめ

| ノイズ種別 | 主な用途 |
|---|---|
| Perlin FBM | 地形高さ、雲、炎、水面 |
| Simplex | Perlin の代替（より高品質） |
| Worley F1 | 岩石テクスチャ、細胞組織 |
| Worley F2-F1 | クラック、ひび割れ |
| SDF + Ray Marching | プロシージャルオブジェクト、ボリュームレンダリング |
| Domain Warp FBM | 煙、有機的な形状 |

---

## 関連ドキュメント
- [15-compute-shaders.md](15-compute-shaders.md) - ノイズのCompute Shader生成
- [14-advanced-rendering.md](14-advanced-rendering.md) - ボリュームフォグ
