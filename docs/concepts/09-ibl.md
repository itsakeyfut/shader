# 09 - IBL（Image-Based Lighting）

## HDR 環境マップ

実際の環境の光を画像として記録し、全方向の光源として使用する。

### フォーマット
```
Equirectangular（等距円筒図法）:
  2:1 の横長画像（360°×180°）
  緯度・経度マッピング
  ファイル形式: .hdr（Radiance RGBE）、.exr（OpenEXR）

Cube Map:
  6面の正方形画像（+X,-X,+Y,-Y,+Z,-Z）
  Direct3D の TextureCube に直接使える
  Equirectangular からの変換が必要
```

### Equirectangular → Cube Map 変換（CS）
```hlsl
// 各キューブ面のテクセルに対応する方向を計算
float3 GetCubeDir(uint face, float2 uv) {
    float2 xy = uv * 2.0 - 1.0; // [0,1] → [-1,1]
    switch(face) {
        case 0: return normalize(float3( 1.0,  xy.y, -xy.x)); // +X
        case 1: return normalize(float3(-1.0,  xy.y,  xy.x)); // -X
        case 2: return normalize(float3( xy.x,  1.0, -xy.y)); // +Y
        case 3: return normalize(float3( xy.x, -1.0,  xy.y)); // -Y
        case 4: return normalize(float3( xy.x,  xy.y,  1.0)); // +Z
        case 5: return normalize(float3(-xy.x,  xy.y, -1.0)); // -Z
    }
    return float3(0,0,0);
}

// Equirectangular → UV
float2 DirToEquirectUV(float3 dir) {
    float phi   = atan2(dir.z, dir.x);  // [-π, π]
    float theta = asin(dir.y);           // [-π/2, π/2]
    return float2(phi / (2.0 * PI) + 0.5, theta / PI + 0.5);
}
```

---

## Irradiance Map（拡散 IBL の事前計算）

Lambertian 拡散に使う放射照度（Irradiance）を全方向について積分し、
Cube Map として事前計算する。

### 数式
```
E(n) = ∫_Ω L_i(ω_i) × (n · ω_i) dω_i
```

### Compute Shader による生成
```hlsl
// 半球サンプリング（積分近似）
[numthreads(8, 8, 6)]
void GenerateIrradiance(uint3 id : SV_DispatchThreadID) {
    uint face = id.z;
    float2 uv = (float2(id.xy) + 0.5) / float(IRRADIANCE_SIZE);
    float3 N  = GetCubeDir(face, uv);

    float3 irradiance = float3(0.0, 0.0, 0.0);
    float  sampleCount = 0;

    // 接線空間の基底ベクトル
    float3 up    = abs(N.y) < 0.999 ? float3(0,1,0) : float3(1,0,0);
    float3 right = normalize(cross(up, N));
    up = cross(N, right);

    // 均一サンプリング（Monte Carlo）
    for (float phi = 0; phi < 2.0 * PI; phi += deltaPhi) {
        for (float theta = 0; theta < 0.5 * PI; theta += deltaTheta) {
            float3 sampleDir = float3(sin(theta)*cos(phi), sin(theta)*sin(phi), cos(theta));
            // タンジェント→ワールド
            float3 worldDir = sampleDir.x*right + sampleDir.y*up + sampleDir.z*N;
            irradiance += envMap.SampleLevel(samp, worldDir, 0).rgb * cos(theta) * sin(theta);
            sampleCount++;
        }
    }
    irradiance = PI * irradiance / sampleCount;

    irradianceMap[uint3(id.xy, face)] = float4(irradiance, 1.0);
}
```

### シェーダーでの使用
```hlsl
// PS でサンプリング（積分済みなので単純サンプル）
float3 irradiance = irradianceMap.Sample(samp, N).rgb;
float3 diffuseIBL = irradiance * albedo * kD;
```

---

## Pre-filtered Environment Map（鏡面 IBL）

Roughness に応じてフィルタリングされた環境反射マップ。
ミップレベル = Roughness として事前計算する。

```
Mip 0 (roughness=0.0): シャープな鏡面反射
Mip 1 (roughness=0.2): わずかにぼけた反射
Mip 2 (roughness=0.4): 中程度のぼけ
...
Mip N (roughness=1.0): 完全拡散（Irradiance Map に近い）
```

### 重点サンプリング（Importance Sampling）
```hlsl
// GGX に沿った方向をサンプリング（均一より効率的）
float3 ImportanceSampleGGX(float2 Xi, float3 N, float roughness) {
    float a = roughness * roughness;

    float phi      = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a*a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    float3 H = float3(cos(phi)*sinTheta, sin(phi)*sinTheta, cosTheta);

    // タンジェント→ワールド
    float3 up    = abs(N.z) < 0.999 ? float3(0,0,1) : float3(1,0,0);
    float3 right = normalize(cross(up, N));
    up = cross(N, right);
    return right*H.x + up*H.y + N*H.z;
}

// Hammersley 低差異列（均一な 2D サンプル分布）
float2 Hammersley(uint i, uint N) {
    uint bits = reversebits(i);
    float ri = float(bits) * 2.3283064365386963e-10; // / 2^32
    return float2(float(i) / float(N), ri);
}
```

### シェーダーでの使用
```hlsl
float3 R = reflect(-V, N); // 反射方向
float  mipLevel = roughness * MAX_REFLECTION_LOD;
float3 prefilteredColor = prefilteredMap.SampleLevel(samp, R, mipLevel).rgb;
```

---

## BRDF 積分 LUT（Split-Sum Approximation）

Brian Karis（Epic Games）の Split-Sum 近似（UE4、SIGGRAPH 2013）。

### 問題
鏡面 BRDF の積分 `∫ f_r(ω_i, ω_o) cos(θ_i) dω_i` はリアルタイムに解けない。

### 分解
```
∫ f_r × cos(θ) dω ≈ (∫ L_i × D × G dω) × (∫ F × cos(θ) dω)
                  ≈ prefilteredColor × (F0 × scale + bias)
```

`scale` と `bias` を `(NdotV, roughness)` の 2D テクスチャとして事前計算 → **BRDF LUT**

### BRDF LUT 生成
```hlsl
float2 IntegrateBRDF(float NdotV, float roughness) {
    float3 V;
    V.x = sqrt(1.0 - NdotV * NdotV);
    V.y = 0.0;
    V.z = NdotV;

    float A = 0.0, B = 0.0;
    float3 N = float3(0.0, 0.0, 1.0);

    [loop]
    for (uint i = 0; i < NUM_SAMPLES; i++) {
        float2 Xi = Hammersley(i, NUM_SAMPLES);
        float3 H  = ImportanceSampleGGX(Xi, N, roughness);
        float3 L  = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = max(L.z, 0.0);
        float NdotH = max(H.z, 0.0);
        float VdotH = max(dot(V, H), 0.0);

        if (NdotL > 0.0) {
            float G     = G_Smith(NdotV, NdotL, roughness);
            float G_Vis = (G * VdotH) / (NdotH * NdotV);
            float Fc    = pow(1.0 - VdotH, 5.0);

            A += (1.0 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }
    return float2(A, B) / float(NUM_SAMPLES);
}
```

### シェーダーでの統合（Split-Sum 完全実装）
```hlsl
float3 CalcSpecularIBL(float3 F0, float NdotV, float roughness, float3 R) {
    float3 prefilteredColor = prefilteredEnvMap.SampleLevel(samp, R, roughness * MAX_LOD).rgb;
    float2 brdf = brdfLUT.Sample(lutSampler, float2(NdotV, roughness)).rg;
    return prefilteredColor * (F0 * brdf.x + brdf.y);
}

// 完全なIBL計算
float3 kS = F_SchlickRoughness(NdotV, F0, roughness);
float3 kD = (1.0 - kS) * (1.0 - metalness);

float3 diffuseIBL  = irradianceMap.Sample(samp, N).rgb * albedo * kD;
float3 specularIBL = CalcSpecularIBL(F0, NdotV, roughness, R);
float3 ambient     = (diffuseIBL + specularIBL) * ao;
```

---

## リアルタイム IBL の実装パターン

### 完全事前計算（オフライン）
- 静的シーン向け
- 最高品質
- 実行時コスト: テクスチャサンプリングのみ

### プローブベース（動的）
- Reflection Capture（UE4 方式）
- 配置したプローブが周囲を撮影して Cube Map 更新
- 負荷: 定期的な更新コスト

### スクリーン空間フォールバック（SSR）
- IBL の精度が不足する場合の補完
- 詳細: [14-advanced-rendering.md](14-advanced-rendering.md)

---

## 関連ドキュメント
- [08-pbr-theory.md](08-pbr-theory.md) - PBR BRDF の数式
- [15-compute-shaders.md](15-compute-shaders.md) - Compute Shader での事前計算
- [../roadmap/03-pbr.md](../roadmap/03-pbr.md) - IBL 実装フェーズ
