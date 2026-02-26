# 概念: Physical Camera（物理カメラ・測光単位）

## 概要

物理ベースレンダリング（PBR）を正しく機能させるには、光の「強さ」を物理的に正確な単位で扱う必要がある。
任意の「輝度」値ではなく、**現実の光量（lux / cd / lm / nits）** でシーンを記述することで、
屋内・屋外・夜間などの環境を一貫した設定で表現できる。
UE5 の物理ライト設定・Auto Exposure・Tone Mapping の正確な理解に必要な概念。

---

## 放射測定（Radiometry）vs 光測定（Photometry）

| 概念 | 放射測定量 | 単位 | 光測定量 | 単位 |
|---|---|---|---|---|
| 全エネルギー流 | 放射束 (Radiant Flux) | W | 光束 (Luminous Flux) | lm（ルーメン） |
| 単位立体角あたり | 放射強度 (Radiant Intensity) | W/sr | 光度 (Luminous Intensity) | cd（カンデラ） |
| 単位面積あたり受けるエネルギー | 照度 (Irradiance) | W/m² | 照度 (Illuminance) | lux（ルクス）= lm/m² |
| 単位面積・単位立体角あたり | 放射輝度 (Radiance) | W/(m²·sr) | 輝度 (Luminance) | cd/m²（ニット = nits） |

**シェーダーで扱う`Lo`（出力ラジアンス）は Radiance（W/m²/sr）または Luminance（cd/m²）に対応する。**

---

## 基本単位の現実値

```
典型的な照度（lux）:
  夜空（月なし）    : 0.001 lux
  満月              : 1 lux
  薄暮              : 10〜100 lux
  室内照明          : 300〜500 lux
  曇天の屋外        : 1,000〜10,000 lux
  晴天の屋外（日陰）: 10,000〜25,000 lux
  直射日光          : 100,000 lux（100 klux）

典型的な輝度（cd/m² = nits）:
  夜空              : 0.001 cd/m²
  月面              : 2,500 cd/m²
  白い紙（室内）    : 100〜300 cd/m²
  LCD モニター      : 300〜1,000 nits
  HDR ディスプレイ  : 1,000〜10,000 nits
  青空              : 8,000 cd/m²
  太陽              : 1.6 × 10^9 cd/m²（直視不能）

光源設定の目安（UE5 lux 設定）:
  Directional Light（晴天太陽）: 100,000 lux
  Directional Light（曇天）    : 10,000 lux
  Point Light（60W 電球相当）  : 830 lm（ルーメン）
  Point Light（LED シーリング）: 2,000〜4,000 lm
```

---

## カメラ露出（Exposure）

### EV（Exposure Value）

EV は「どれだけ光を取り込むか」を 1 つの数値で表す対数スケールの指標。

```
EV₁₀₀ = log₂(N² / t) - log₂(S / 100)
  N: F値（絞り）
  t: シャッタースピード（秒）
  S: ISO 感度
```

EV が 1 増えると、露出量は半分（1 段暗く）。

```
典型的な EV₁₀₀:
  EV 0  : 暗い室内（夜、キャンドル）
  EV 5  : 室内照明
  EV 10 : 曇天屋外 / 日陰
  EV 14 : 晴天屋外の標準露出
  EV 16 : 雪上 / 砂浜の直射日光
```

### EV₁₀₀ → Exposure 変換（シェーダー）

```hlsl
// EV100 から線形露出倍率を計算
// L_avg: 平均輝度 (cd/m²) を想定したスケール
float EV100ToExposure(float ev100) {
    // EV₁₀₀ = log₂(L_avg × 100 / 12.5)
    // → L_avg = 2^EV100 × 12.5 / 100
    // Exposure = 1 / (L_avg × q)  ここで q ≈ 1.0（センサー特性）
    return 1.0 / (pow(2.0, ev100) * 1.2);
}

float3 ApplyExposure(float3 hdrColor, float ev100) {
    return hdrColor * EV100ToExposure(ev100);
}
```

### 絞り / シャッタースピード / ISO の関係

```
露出の等価交換（Sunny 16 ルール）:
  EV = log₂(f-number² / shutter_speed) + log₂(ISO / 100)

例: f/16, 1/100s, ISO 100 → EV₁₀₀ = log₂(256 / 0.01) + 0 ≈ 14.6
   （晴天の標準露出）

絞りの役割:
  f/1.4: 明るい（多くの光）, DoF 浅い
  f/2.8: 標準ポートレート
  f/8:   風景写真の標準（全体にピント）
  f/22:  暗い（少ない光）, DoF 深い

シャッタースピード:
  1/1000s: 動体凍結
  1/60s:   動体が若干流れる
  1/4s:    静止シーン, 三脚必要
  30s:     星空・光跡
```

---

## Auto Exposure（自動露出）

シーンの平均輝度を計算し、EV₁₀₀ を自動調整する。

### ヒストグラムベース Auto Exposure（CS）

```hlsl
// パス 1: ヒストグラム構築 CS
// ログ輝度を 256 ビンのヒストグラムに集計

#define NUM_BINS 256
groupshared uint histogram[NUM_BINS];

[numthreads(16, 16, 1)]
void BuildHistogramCS(uint2 tid : SV_DispatchThreadID, uint gtid : SV_GroupIndex) {
    if (gtid < NUM_BINS) histogram[gtid] = 0;
    GroupMemoryBarrierWithGroupSync();

    if (all(tid < uint2(screenWidth, screenHeight))) {
        float3 hdr      = hdrTexture[tid].rgb;
        float  lum      = dot(hdr, float3(0.2126, 0.7152, 0.0722));
        float  logLum   = log2(max(lum, 0.0001)); // 例: -10 〜 +20 の範囲
        float  normalized = saturate((logLum - LOG_MIN) / (LOG_MAX - LOG_MIN));
        uint   bin      = (uint)(normalized * (NUM_BINS - 1));
        InterlockedAdd(histogram[bin], 1);
    }
    GroupMemoryBarrierWithGroupSync();

    if (gtid < NUM_BINS)
        InterlockedAdd(histogramBuffer[gtid], histogram[gtid]);
}

// パス 2: 平均輝度 → EV100 算出 CS
[numthreads(NUM_BINS, 1, 1)]
void ComputeAverageEVCS(uint tid : SV_DispatchThreadID) {
    // 下位 / 上位の外れ値を除外した平均（Trim Percentile）
    uint   totalPixels  = screenWidth * screenHeight;
    uint   lowCutoff    = totalPixels * 0.05;  // 下位 5% 除外
    uint   highCutoff   = totalPixels * 0.95; // 上位 5% 除外

    // prefix sum でカウント、範囲内の輝度の加重平均
    // ... （実装省略）

    float avgLogLum = /* 計算結果 */;
    float avgLum    = exp2(avgLogLum);

    // EV100: L × 100 / 12.5 の log2
    float ev100     = log2(avgLum * 100.0 / 12.5);

    // Smooth adaptation（時間補間）
    float prevEV    = currentEVBuffer[0];
    float adaptedEV = lerp(prevEV, ev100, 1.0 - exp(-deltaTime / adaptationSpeed));
    currentEVBuffer[0] = adaptedEV;
}
```

### Auto Exposure のピクセルシェーダー適用

```hlsl
float4 ToneMappingPS(float2 uv : TEXCOORD0) : SV_Target {
    float3 hdr     = hdrBuffer.Sample(linearSampler, uv).rgb;
    float  ev100   = currentEVBuffer[0];
    float  exposure = EV100ToExposure(ev100);

    float3 exposed  = hdr * exposure;

    // ACES Filmic Tone Mapping
    float3 ldr = ACESFilmic(exposed);

    // Gamma Correction（出力が sRGB ならここで）
    return float4(LinearToSRGB(ldr), 1.0);
}
```

---

## 物理的な光源強度の設定

### 点光源（Point Light）

```hlsl
// 点光源の照度計算（Inverse Square Law）
// Φ: 光束 (lm), d: 距離 (m)
// 照度 E = Φ / (4π × d²)  [lux]

float PointLightIlluminance(float lumens, float distance) {
    return lumens / (4.0 * PI * distance * distance);
}

// シェーダーでの使用
float luminousFlux = 800.0;  // 60W 白熱球相当 lm
float dist         = length(worldPos - lightPos);
float illuminance  = PointLightIlluminance(luminousFlux, dist);
float3 irradiance  = lightColor * illuminance * NdotL;
```

### 指向性ライト（Directional Light）

```hlsl
// 太陽: 照度 = 100,000 lux（設定値をそのまま使用）
// シェーダーでは lux 値を直接 irradiance として使用
float3 directIrradiance = sunColor * sunIlluminance * max(0, dot(N, L));
```

---

## UE5 での物理ライト設定

```
UE5 Light プロパティ:
  Intensity Units:
    - Unitless: 任意スケール（旧来の方法）
    - Candelas: cd（光度）
    - Lumens: lm（光束）- Point/Spot Light 推奨
    - EV: 露出値で直接指定

  PostProcessVolume:
    Exposure Compensation: 手動 EV シフト
    Min/Max EV100: Auto Exposure の範囲
    Metering Mode: Auto Exposure 手法（Histogram / Basic）
    Low/High Percent: Trim percentile 設定
```

---

## Filmic Response（ACES）と物理単位の関係

```
[物理単位の流れ]
  光源（lux / lm / cd）
      ↓ BRDF × NdotL × 距離減衰
  Radiance（cd/m²）としてフレームバッファへ
      ↓ Auto Exposure（EV100 → 倍率）
  0 付近に正規化された Radiance
      ↓ ACES Tone Mapping
  0〜1 の LDR 値
      ↓ Gamma / sRGB 変換
  ディスプレイ出力
```

ACES の入力は EV₁₀₀ で露出済みの Radiance を想定して調整されているため、
物理単位（lux/lm）で設定した光源と組み合わせることで正しい絵作りが可能。

---

## 関連ドキュメント

- [06-lighting-models.md](06-lighting-models.md) — 光源の減衰・NdotL
- [08-pbr-theory.md](08-pbr-theory.md) — BRDF・エネルギー保存
- [11-post-processing.md](11-post-processing.md) — Tone Mapping・HDR
- [roadmap/04-post-process.md](../roadmap/04-post-process.md) — Post Process 実装
