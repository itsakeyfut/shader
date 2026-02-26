# Roadmap: Phase 9 - Environment Rendering（環境レンダリング）

**プロジェクト**: `D:/dev/shader/environment-rendering/`
**API**: Direct3D 12
**目標**: 地形・水・大気・植生など屋外環境の全要素をシェーダーで実装する

---

## 習得する概念

| 概念 | 関連ドキュメント |
|---|---|
| テクスチャサンプリング（仮想テクスチャ） | [05-texturing-sampling.md](../concepts/05-texturing-sampling.md) |
| テッセレーション（HS/DS） | [00-graphics-pipeline.md](../concepts/00-graphics-pipeline.md) |
| Compute Shader（FFT・シミュレーション） | [15-compute-shaders.md](../concepts/15-compute-shaders.md) |
| プロシージャルノイズ | [17-procedural-noise.md](../concepts/17-procedural-noise.md) |

---

## フェーズ分け

### フェーズ 9-1: 地形（Terrain）レンダリング

**実装項目**:
- 高さマップ（Heightmap）から地形メッシュを生成（CS）
- CD-LOD / CLOD（連続的な LOD 遷移）
- テッセレーション（HS/DS）で視点近くを細分化
- テクスチャスプラッティング（最大 8 レイヤー、Weight Map ブレンド）
- Macro / Micro ノーマルの合成（Reoriented Normal Mapping）

```hlsl
// Terrain HS: 視点距離に応じてテッセレーション係数を決定
struct HSConstOutput {
    float edgeTess[3] : SV_TessFactor;
    float innerTess   : SV_InsideTessFactor;
};

HSConstOutput TerrainHSConst(InputPatch<VSOutput, 3> patch) {
    HSConstOutput output;

    // カメラ距離でテッセレーション係数を計算
    float3 center = (patch[0].worldPos + patch[1].worldPos + patch[2].worldPos) / 3.0;
    float  dist   = distance(center, cameraPos);
    float  tess   = clamp(lerp(maxTess, minTess, (dist - minDist) / (maxDist - minDist)), minTess, maxTess);

    output.edgeTess[0] = output.edgeTess[1] = output.edgeTess[2] = tess;
    output.innerTess   = tess;
    return output;
}

// Terrain DS: 高さマップで頂点を変位させる
[domain("tri")]
VSOutput TerrainDS(HSConstOutput hsData, OutputPatch<VSOutput, 3> patch,
                   float3 bary : SV_DomainLocation) {
    float3 pos = bary.x * patch[0].worldPos + bary.y * patch[1].worldPos + bary.z * patch[2].worldPos;
    float2 uv  = bary.x * patch[0].uv + bary.y * patch[1].uv + bary.z * patch[2].uv;

    float height = heightMap.SampleLevel(samp, uv, 0).r * heightScale;
    pos.y += height;

    output.position = mul(viewProj, float4(pos, 1.0));
    return output;
}
```

---

### フェーズ 9-2: 地形テクスチャスプラッティング

**実装項目**:
- Weight Map（各チャンネルがレイヤー重み）のサンプリング
- Height-Blending（高さに応じて境界を鋭く）
- TriPlanar マッピング（急勾配面の UV 引き伸ばし防止）
- Virtual Texture（Mega Texture）の概念理解

```hlsl
// Height-Blended スプラッティング（自然な素材境界）
float4 HeightBlend(float4 colorA, float heightA, float4 colorB, float heightB, float blend) {
    float ha = heightA + (1.0 - blend);
    float hb = heightB + blend;
    float threshold = max(ha, hb) - 0.1;
    float wa = max(ha - threshold, 0.0);
    float wb = max(hb - threshold, 0.0);
    return (colorA * wa + colorB * wb) / (wa + wb);
}

// TriPlanar マッピング
float3 TriPlanarSample(Texture2D tex, float3 pos, float3 N, float scale) {
    float2 uvX = pos.yz * scale;
    float2 uvY = pos.xz * scale;
    float2 uvZ = pos.xy * scale;

    float3 cX = tex.Sample(samp, uvX).rgb;
    float3 cY = tex.Sample(samp, uvY).rgb;
    float3 cZ = tex.Sample(samp, uvZ).rgb;

    float3 blend = pow(abs(N), 4.0);
    blend /= dot(blend, 1.0);

    return cX * blend.x + cY * blend.y + cZ * blend.z;
}
```

---

### フェーズ 9-3: 水（Water）レンダリング

**実装項目**:
- Gerstner 波（正弦波の重ね合わせ、頂点変位）
- 法線マップの重ね合わせ（流れる波紋、2枚のスクロール）
- フォーム（泡）: 水深・波頂部・浅瀬で出現
- 水中の屈折（Refraction Buffer）
- Fresnel による水面反射（IBL + SSR + RT Reflection との合成）
- 水面下のオブジェクトの深度フォグ（透明度 / 色のフォールオフ）

```hlsl
// Gerstner Wave（1波分）
struct Wave {
    float2 direction;
    float  amplitude;
    float  wavelength;
    float  speed;
    float  steepness; // 0=正弦波, 1=トロコイド（尖った波頭）
};

float3 GerstnerWave(Wave w, float3 pos, float time, inout float3 tangent, inout float3 binormal) {
    float k    = 2.0 * PI / w.wavelength;
    float c    = sqrt(9.8 / k); // 深水波の位相速度
    float2 d   = normalize(w.direction);
    float  f   = k * (dot(d, pos.xz) - c * time);
    float  a   = w.steepness / k;

    tangent  += float3(-d.x * d.x * (w.steepness * sin(f)), d.x * (w.steepness * cos(f)),
                        -d.x * d.y * (w.steepness * sin(f)));
    binormal += float3(-d.x * d.y * (w.steepness * sin(f)), d.y * (w.steepness * cos(f)),
                        -d.y * d.y * (w.steepness * sin(f)));

    return float3(d.x * (a * cos(f)), a * sin(f), d.y * (a * cos(f)));
}

float4 WaterPS(VSOutput input) : SV_Target {
    // 屈折（背景テクスチャをオフセット）
    float3 N = SampleWaterNormal(input.uv, time);
    float2 refractionOffset = N.xy * refractionStrength;
    float3 refracted = refractionTex.Sample(samp, input.screenUV + refractionOffset).rgb;

    // Fresnel
    float3 V       = normalize(cameraPos - input.worldPos);
    float  fresnel = pow(1.0 - saturate(dot(N, V)), 5.0) * 0.98 + 0.02;

    // 反射（RT Reflection or SSR or Cube Map）
    float3 reflected = reflectionTex.Sample(samp, reflect(-V, N)).rgb;

    // 水中のフォグ
    float waterDepth = waterDepthTex.Sample(samp, input.screenUV).r;
    float3 waterColor = lerp(shallowColor, deepColor, saturate(waterDepth / maxDepth));
    refracted = lerp(refracted, waterColor, saturate(waterDepth * fogDensity));

    return float4(lerp(refracted, reflected, fresnel), 1.0);
}
```

---

### フェーズ 9-4: FFT 海洋シミュレーション

**実装項目**:
- Phillips スペクトル（波の周波数分布）の初期化
- GPU FFT（Cooley-Tukey アルゴリズム、Compute Shader）
- 頂点変位（XYZ）+ ヤコビアンで泡マスクを計算
- タイリング + 複数スケールの組み合わせ

```hlsl
// Phillips スペクトル（初期化 CS）
[numthreads(16, 16, 1)]
void InitSpectrumCS(uint3 dtID : SV_DispatchThreadID) {
    int2  k    = int2(dtID.xy) - N / 2;
    float kLen = length(float2(k)) * (2.0 * PI / L);
    if (kLen < 0.001) { h0k[dtID.xy] = 0; return; }

    float kDotW  = dot(normalize(float2(k)), windDir);
    float L_     = windSpeed * windSpeed / 9.8;
    float Ph     = A * exp(-1.0 / (kLen * L_) / (kLen * L_))
                   / (kLen * kLen * kLen * kLen) * kDotW * kDotW;

    float2 rnd   = GaussianRandom(dtID.xy, seed);
    h0k[dtID.xy] = float2(rnd * sqrt(Ph * 0.5));
}
```

---

### フェーズ 9-5: 大気散乱（Atmospheric Scattering）

**実装項目**:
- Rayleigh 散乱（大気分子による青い空）
- Mie 散乱（エアロゾルによる太陽周辺の散乱光）
- Bruneton & Neyret モデルの簡易実装（事前計算テーブル）
- 時刻 / 太陽角度に応じた空の色変化（日の出・日没）
- 地平線ヘイズ

```hlsl
// Rayleigh + Mie 散乱の係数
static const float3 betaR = float3(5.8e-6, 13.5e-6, 33.1e-6); // Rayleigh
static const float  betaM = 21e-6;                              // Mie

float3 GetSkyColor(float3 rayDir, float3 sunDir) {
    float  mu    = dot(rayDir, sunDir);
    float  phaseR = 3.0 / (16.0 * PI) * (1.0 + mu * mu);
    float  g     = 0.76; // 前方散乱係数
    float  phaseM = 3.0 / (8.0 * PI) * ((1 - g*g) * (1 + mu*mu))
                    / ((2 + g*g) * pow(1 + g*g - 2*g*mu, 1.5));

    // 大気を Ray March（簡易）
    float3 scatter = float3(0, 0, 0);
    float  tStep   = atmosphereRadius / NUM_SAMPLES;
    float3 pos     = cameraPos;

    for (int i = 0; i < NUM_SAMPLES; i++) {
        float h    = max(length(pos) - earthRadius, 0.0);
        float Hr   = exp(-h / HR); // Rayleigh 高度スケール
        float Hm   = exp(-h / HM); // Mie 高度スケール
        scatter   += (betaR * Hr * phaseR + betaM * Hm * phaseM) * sunColor * tStep;
        pos       += rayDir * tStep;
    }
    return scatter;
}
```

---

### フェーズ 9-6: ボリューメトリッククラウド

**実装項目**:
- 3D Noise テクスチャ（Perlin/Worley の組み合わせ）でクラウド密度生成
- Ray Marching（フラスタム内のボリュームをステップ）
- Beer-Lambert 法則による光の減衰
- マルチスキャッタリング近似（Henyey-Greenstein 複数ローブ）
- TAA での時間的ノイズ平均化

```hlsl
// Beer-Lambert 吸収
float BeerLambert(float density, float depth) {
    return exp(-density * depth);
}

// クラウドレイマーチ（概略）
float4 CloudPS(float2 uv : TEXCOORD0) : SV_Target {
    float3 rayOrigin = cameraPos;
    float3 rayDir    = GetRayDir(uv);

    float3 transmittance = float3(1, 1, 1);
    float3 scattering    = float3(0, 0, 0);
    float  t             = cloudStartHeight;

    for (int i = 0; i < NUM_CLOUD_STEPS; i++) {
        float3 pos     = rayOrigin + rayDir * t;
        float  density = SampleCloudDensity(pos);

        if (density > 0.01) {
            float  shadow  = SampleCloudShadow(pos, sunDir); // 光の透過
            float3 radiance = sunColor * shadow * density;
            scattering    += transmittance * radiance * stepSize;
            transmittance *= BeerLambert(density, stepSize);
        }

        t += stepSize;
        if (all(transmittance < 0.01)) break;
    }
    return float4(scattering, 1.0 - luminance(transmittance));
}
```

---

### フェーズ 9-7: 植生（Foliage）レンダリング

**実装項目**:
- Billboard（カメラ向き 2 面または 8 面） + LOD
- Alpha Test（Dithered LOD 遷移）
- Wind Animation（頂点シェーダーでの揺れ）
  - 幹: 大きな揺れ
  - 枝: 中程度の揺れ
  - 葉: 細かい揺れ（Ambient Occlusion に応じて）
- Two-Sided Foliage（透過光 / 薄葉の透過シェーディング）
- GPU インスタンシング（数万本のオブジェクト）

```hlsl
// 葉の風揺れ（頂点シェーダー）
float3 AnimateWind(float3 worldPos, float3 normal, float2 uv, float windAO) {
    float  phase      = dot(worldPos.xz, float2(0.7, 0.3)) * 0.5;
    float  wave1      = sin(time * windFrequency       + phase) * windStrength;
    float  wave2      = sin(time * windFrequency * 2.3 + phase * 1.4) * windStrength * 0.4;
    float  windAmount = (wave1 + wave2) * windAO; // AO が小さい = 隠れた部分 = 揺れにくい

    float3 windDir    = normalize(float3(1, 0, 0.5));
    return worldPos + windDir * windAmount;
}

// Two-Sided Foliage Shading（葉の透過光）
float4 FoliagePS(VSOutput input) : SV_Target {
    float3 N = normalize(input.normal);
    if (!input.isFrontFace) N = -N; // 裏面法線を反転

    // 表面の拡散ライティング
    float NdotL = max(dot(N, L), 0.0);
    float3 diffuse = albedo * lightColor * NdotL;

    // 葉の透過（裏面から光が透けてくる）
    float3 backLight = max(dot(-N, L), 0.0) * transmissionColor * subsurface;
    diffuse += backLight;

    return float4(diffuse, alpha);
}
```

---

### フェーズ 9-8: 草（Grass）シェーダー

**実装項目**:
- Geometry Shader または Mesh Shader で草ブレードを生成
- SDF に基づくキャラクターとの当たり判定（押しつぶし効果）
- LOD（近: 3D ブレード, 中: Billboard, 遠: テクスチャ）
- GPU Culling（視錐台 + オクルージョン）

---

### フェーズ 9-9: 雪・雨・砂嵐（ウェザーエフェクト）

**実装項目**:
- 雪の積もり（法線方向で上向きに白い素材をブレンド）
- 雨のウェット表面（Roughness を下げてリフレクション増加）
- 波紋（Ripple）: Ring 状の法線アニメーション
- 砂嵐のパーティクル（Compute Shader、百万粒子）

---

## ファイル構成（完成時）

```
environment-rendering/
├── CMakeLists.txt
├── src/
│   ├── Terrain.cpp/.h
│   ├── Water.cpp/.h
│   ├── OceanFFT.cpp/.h
│   ├── Atmosphere.cpp/.h
│   ├── VolumetricCloud.cpp/.h
│   └── Foliage.cpp/.h
└── shaders/
    ├── terrain_vs.hlsl / terrain_hs.hlsl / terrain_ds.hlsl / terrain_ps.hlsl
    ├── water_vs.hlsl / water_ps.hlsl
    ├── ocean_spectrum_cs.hlsl / ocean_fft_cs.hlsl
    ├── atmosphere_ps.hlsl
    ├── cloud_ps.hlsl
    ├── foliage_vs.hlsl / foliage_ps.hlsl
    └── grass_ms.hlsl  ← Mesh Shader
```

---

## 確認チェックリスト

- [ ] 地形のテッセレーション係数が視点に近いほど高くなる
- [ ] スプラッティングの境界が Height-Blend で自然に見える
- [ ] Gerstner 波が複数合成されて自然な波形になっている
- [ ] Fresnel で水平視線方向に強い反射が出る
- [ ] 空の色が太陽角度（時刻）に応じて変化する
- [ ] 植生のインスタンシングで 1 万本以上が 60fps で描画できる

---

## 関連ドキュメント
- [08-character-rendering.md](08-character-rendering.md) - 前フェーズ
- [10-vfx-rendering.md](10-vfx-rendering.md) - 次フェーズ
- [../concepts/17-procedural-noise.md](../concepts/17-procedural-noise.md)
- [../concepts/15-compute-shaders.md](../concepts/15-compute-shaders.md)
