# 11 - Post-Processing（ポストプロセス）

## Render to Texture（RTT）フレームワーク

最終画面に直接描画するのではなく、テクスチャに描画してから後処理する。

```cpp
// HDR レンダーターゲット作成
D3D11_TEXTURE2D_DESC rtDesc = {};
rtDesc.Width     = width;
rtDesc.Height    = height;
rtDesc.Format    = DXGI_FORMAT_R16G16B16A16_FLOAT; // HDR（Float16）
rtDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

// MRT（Multiple Render Targets）
ID3D11RenderTargetView* rtvs[3] = { colorRTV, normalRTV, velocityRTV };
context->OMSetRenderTargets(3, rtvs, dsv);
```

---

## Full-screen Quad / Triangle Pass

ポストプロセスは画面全体に1枚の四角形（または三角形）を描画する。

```hlsl
// フルスクリーン三角形（頂点バッファ不要、SV_VertexID で生成）
void FullscreenVS(
    uint id : SV_VertexID,
    out float4 pos : SV_Position,
    out float2 uv  : TEXCOORD0
) {
    // 3頂点で画面全体をカバーする大きな三角形
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos  = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}
```

---

## HDR バッファと Exposure

### HDR レンダリング
```hlsl
// シーンを HDR バッファ（R16G16B16A16_FLOAT）に描画
// 値は 1.0 を超えることが許容される
float3 hdrColor = lighting * exposure; // exposure: カメラの露出
```

### 平均輝度の計算（自動露出）
```hlsl
// Compute Shader でヒストグラム or 対数平均を計算
float luminance = dot(color, float3(0.2126, 0.7152, 0.0722)); // Rec.709
float logLum    = log(max(luminance, 0.0001));
// ダウンサンプリングチェーン or Atomic で平均化
```

---

## Tone Mapping

HDR 値を表示可能な LDR（[0,1]）に変換する。

### Reinhard
```hlsl
float3 ReinhardToneMap(float3 hdr) {
    return hdr / (hdr + 1.0);
}
```

### ACES Filmic（Unreal Engine、映画品質）
```hlsl
// Stephen Hill による ACES RRT+ODT フィット
float3 ACESFilmic(float3 x) {
    float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}
```

### Neutral（シンプルな Filmic）
```hlsl
float3 NeutralToneMap(float3 x) {
    float3 a = 2.0 * x * x;
    float3 b = x + 0.2;
    return saturate(a / (b * b) * 1.2);
}
```

---

## Gamma Correction・Linear Workflow

### Linear Workflow の必要性
```
アーティストは sRGB（ガンマ空間）でテクスチャを作成
シェーダー計算は Linear 空間で行う必要がある

sRGB → Linear: color = pow(color, 2.2)  （Texture2D の sRGB フォーマット指定で自動）
Linear → sRGB: color = pow(color, 1/2.2) （または DXGI_FORMAT の _SRGB スワップチェーン）
```

```hlsl
// 手動ガンマ補正（SDR 出力時）
float3 ldrColor = ACESFilmic(hdrColor);
float3 srgbColor = pow(max(ldrColor, 0.0), 1.0 / 2.2);

// D3D11 では DXGI_FORMAT_R8G8B8A8_UNORM_SRGB のスワップチェーンで自動変換可能
```

---

## Bloom（光の滲み）

輝度の高い部分から光が溢れ出る効果。

### パイプライン
1. **Threshold**: 輝度閾値を超えたピクセルを抽出
2. **Blur**: ガウスブラーまたは Dual Kawase ブラー
3. **Blend**: 元画像にブルームを加算

```hlsl
// ステップ 1: 輝度抽出
float4 ThresholdPS(float2 uv : TEXCOORD0) : SV_Target {
    float3 color = hdrBuffer.Sample(samp, uv).rgb;
    float lum    = dot(color, float3(0.2126, 0.7152, 0.0722));
    float factor = max(lum - threshold, 0.0) / max(lum, 0.0001);
    return float4(color * factor, 1.0);
}
```

### Dual Kawase ブラー（高品質・低コスト）
```hlsl
// ダウンサンプル
float4 DownsamplePS(float2 uv : TEXCOORD0) : SV_Target {
    float2 texelSize = 1.0 / resolution;
    float4 sum = float4(0, 0, 0, 0);
    sum += bloom.Sample(samp, uv + float2(-1, -1) * texelSize) * 0.25;
    sum += bloom.Sample(samp, uv + float2( 1, -1) * texelSize) * 0.25;
    sum += bloom.Sample(samp, uv + float2(-1,  1) * texelSize) * 0.25;
    sum += bloom.Sample(samp, uv + float2( 1,  1) * texelSize) * 0.25;
    return sum;
}

// アップサンプル（バタフライパターン）
float4 UpsamplePS(float2 uv : TEXCOORD0) : SV_Target {
    float2 ts = 1.0 / resolution;
    float4 sum = float4(0, 0, 0, 0);
    sum += bloom.Sample(samp, uv + float2(-2, 0) * ts) * 0.0625;
    sum += bloom.Sample(samp, uv + float2( 0, 0) * ts) * 0.125;
    sum += bloom.Sample(samp, uv + float2( 2, 0) * ts) * 0.0625;
    // ... 計8サンプル
    return sum;
}
```

---

## Depth of Field（被写界深度）

焦点距離から外れたオブジェクトがぼけて見える効果。

```hlsl
// Circle of Confusion（CoC）の計算
float CalcCoC(float depth, float focusDistance, float aperture, float focalLength) {
    float df = focusDistance;
    float coc = abs(aperture * (focalLength * (depth - df)) / (depth * (df - focalLength)));
    return coc; // ピクセル単位
}

// CoC に基づいてブラー半径を決定
float4 DoFPS(float2 uv : TEXCOORD0) : SV_Target {
    float depth = depthBuffer.Sample(pointSampler, uv).r;
    float linearDepth = LinearizeDepth(depth, nearZ, farZ);
    float coc   = CalcCoC(linearDepth, focusDistance, aperture, focalLength);

    // CoC に応じたカーネルサイズでサンプリング
    float3 color = GatherSamples(colorBuffer, uv, coc);
    return float4(color, 1.0);
}
```

---

## Motion Blur

カメラまたはオブジェクトの動きによるブラー。

```hlsl
// Velocity Buffer（G-Buffer パスで生成）
float2 CalcVelocity(float4 currentClipPos, float4 prevClipPos) {
    float2 current = currentClipPos.xy / currentClipPos.w;
    float2 prev    = prevClipPos.xy   / prevClipPos.w;
    return (current - prev) * 0.5; // NDC の差分 → UV スペース
}

// Motion Blur パス
float4 MotionBlurPS(float2 uv : TEXCOORD0) : SV_Target {
    float2 velocity   = velocityBuffer.Sample(samp, uv).rg;
    float  numSamples = 8.0;
    float3 color      = float3(0, 0, 0);

    for (float i = 0.0; i < numSamples; i++) {
        float  t      = (i / (numSamples - 1.0)) - 0.5;
        float2 offset = velocity * t;
        color += colorBuffer.Sample(samp, uv + offset).rgb;
    }
    return float4(color / numSamples, 1.0);
}
```

---

## Color Grading（3D LUT）

映画的な色補正。全ての入力色を 3D テクスチャでルックアップして変換。

```hlsl
// 3D LUT サンプリング
float3 ColorGrade(float3 color, Texture3D lut, SamplerState samp, float lutSize) {
    // [0,1] → LUT テクセル中心に補正
    float scale  = (lutSize - 1.0) / lutSize;
    float offset = 0.5 / lutSize;
    float3 uvw   = color * scale + offset;
    return lut.Sample(samp, uvw).rgb;
}

// PS での適用
float4 ColorGradingPS(float2 uv : TEXCOORD0) : SV_Target {
    float3 tonemapped = ACESFilmic(hdrBuffer.Sample(samp, uv).rgb);
    float3 graded     = ColorGrade(tonemapped, colorLUT, lutSampler, 32.0);
    return float4(graded, 1.0);
}
```

### LUT 生成
- Resolve（DaVinci）や Adobe Premiere でパラメータ調整 → .cube ファイル
- Identity LUT から開始（R=X, G=Y, B=Z の 32×32×32 テクスチャ）

---

## 標準的なポストプロセスチェーン

```
[Scene HDR Buffer]
        ↓
[Shadow / Lighting Pass]
        ↓
[SSAO（Screen Space AO）]
        ↓
[Bloom Threshold + Blur]
        ↓
[Motion Blur]
        ↓
[Depth of Field]
        ↓
[Tone Mapping（ACES）]
        ↓
[TAA（Temporal Anti-Aliasing）]
        ↓
[Color Grading（3D LUT）]
        ↓
[FXAA / Sharpening]
        ↓
[Gamma Correction → Display]
```

---

## 関連ドキュメント
- [12-anti-aliasing.md](12-anti-aliasing.md) - FXAA / TAA
- [13-ambient-occlusion.md](13-ambient-occlusion.md) - SSAO
- [../roadmap/04-post-process.md](../roadmap/04-post-process.md) - 実装フェーズ
