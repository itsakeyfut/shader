# 12 - Anti-Aliasing（アンチエイリアシング）

## MSAA（Multisample Anti-Aliasing）

ハードウェアが各ピクセルを複数のサブサンプルで評価してエッジを滑らかにする。

```
ピクセル 1つ → 4サブサンプル (4x MSAA):
  ┌─────────┐
  │ ×   ×  │  × = サブサンプル位置
  │         │
  │ ×   ×  │
  └─────────┘
  PS は1回実行、カバレッジとデプスは4サンプルで評価
```

```cpp
// MSAA バッファ作成
D3D11_TEXTURE2D_DESC msDesc = {};
msDesc.SampleDesc.Count   = 4; // 4x MSAA
msDesc.SampleDesc.Quality = D3D11_STANDARD_MULTISAMPLE_PATTERN;
msDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

// MSAA → 非 MSAA へのリゾルブ
context->ResolveSubresource(destTex, 0, msaaTex, 0, DXGI_FORMAT_R8G8B8A8_UNORM);
```

### MSAA の限界
- ポストプロセスとの相性が悪い（MSAA テクスチャをシェーダーで読む際に注意）
- Deferred Shading と組み合わせると複雑
- アルファテスト（`clip()`）のエッジには効かない → Alpha-to-Coverage が必要
- メモリ消費: 4x MSAA で通常の4倍のバッファ

---

## FXAA（Fast Approximate Anti-Aliasing）

NVIDIA が開発した画面空間後処理 AA。最終画像のエッジを検出してぼかす。

```hlsl
// ルミナンス計算
float FxaaLuma(float3 color) {
    return dot(color, float3(0.299, 0.587, 0.114)); // NTSC 輝度
}

float4 FxaaPS(float2 uv : TEXCOORD0) : SV_Target {
    float2 texelSize = 1.0 / screenSize;

    // 近傍ルミナンスサンプリング
    float lumCenter = FxaaLuma(tex.Sample(samp, uv).rgb);
    float lumN = FxaaLuma(tex.Sample(samp, uv + float2( 0, -1) * texelSize).rgb);
    float lumS = FxaaLuma(tex.Sample(samp, uv + float2( 0,  1) * texelSize).rgb);
    float lumE = FxaaLuma(tex.Sample(samp, uv + float2( 1,  0) * texelSize).rgb);
    float lumW = FxaaLuma(tex.Sample(samp, uv + float2(-1,  0) * texelSize).rgb);

    // エッジ検出
    float maxLum = max(max(max(max(lumN, lumS), lumE), lumW), lumCenter);
    float minLum = min(min(min(min(lumN, lumS), lumE), lumW), lumCenter);
    float range  = maxLum - minLum;

    // 閾値以下はスキップ（非エッジ）
    if (range < max(FXAA_EDGE_MIN, maxLum * FXAA_EDGE_THRESHOLD))
        return float4(tex.Sample(samp, uv).rgb, 1.0);

    // エッジ方向に沿ってサンプリング...
    // （完全な FXAA は 100+ 行）
    // 参考: Timothy Lottes の FXAA 3.11
}
```

**特徴**:
- 軽量・シンプル
- テキストやUIのエッジをぼかしてしまう（過剰なぼけ）
- TAA の普及でゲームでの使用は減少

---

## SMAA（Subpixel Morphological Anti-Aliasing）

形態学的エッジ検出によるより高品質な画面空間 AA。
3パスで処理する。

```
Pass 1: Edge Detection（エッジ検出）
  → ルミナンスまたは色のコントラストでエッジ検出
  → EdgeTex に格納

Pass 2: Blending Weight Calculation（ブレンド重み計算）
  → エッジのパターンを分析（L字・ジグザグ等）
  → BlendTex に最適な重みを計算

Pass 3: Neighborhood Blending（隣接ブレンド）
  → BlendTex を使って隣接ピクセルをブレンド
```

```hlsl
// Pass 3: ブレンド（例）
float4 SMAANeighborhoodBlendingPS(float2 uv : TEXCOORD0) : SV_Target {
    float4 blendWeights = blendTex.Sample(samp, uv);

    // 上下左右にブレンド
    float4 color = float4(0, 0, 0, 0);
    color += colorTex.Sample(samp, uv + float2(0, blendWeights.x)) * blendWeights.x;
    // ...
    return color;
}
```

**SMAA の利点**: FXAA より品質が高く、テキストのぼけが少ない
**実用**: SMAA T2× で TAA 相当の品質も可能

---

## TAA（Temporal Anti-Aliasing）

複数フレームの情報を積み重ねて高品質な AA を実現する。
現代ゲームエンジンの標準。

### 基本アルゴリズム

```
フレーム N-1:  TAA済み画像（HistoryBuffer）
フレーム N:    現在フレーム（Jitterあり）
              ↓
         リプロジェクション
              ↓
        履歴サンプリング + 現在フレームブレンド
              ↓
         TAA 済み画像 → HistoryBuffer に保存
```

### ジッター（Jitter）
毎フレーム、プロジェクション行列のオフセットをわずかにずらす:

```cpp
// Halton シーケンス（低差異列）でジッター
float2 jitter = HaltonSequence(frameCount % 16) * 2.0 - 1.0;
jitter /= float2(screenWidth, screenHeight);

// 投影行列のオフセットに追加
projMatrix[2][0] += jitter.x;
projMatrix[2][1] += jitter.y;
```

### リプロジェクション（Reprojection）

```hlsl
// 前フレームの対応ピクセル位置を計算
float2 CalcReprojectedUV(float2 uv, float depth, float4x4 prevViewProj, float4x4 invViewProj) {
    // 現在フレームのワールド座標を復元
    float4 clipPos   = float4(uv * 2.0 - 1.0, depth, 1.0);
    clipPos.y        = -clipPos.y; // D3D Y 反転
    float4 worldPos  = mul(invViewProj, clipPos);
    worldPos        /= worldPos.w;

    // 前フレームのクリップ空間に投影
    float4 prevClip = mul(prevViewProj, worldPos);
    float2 prevNDC  = (prevClip.xy / prevClip.w) * float2(0.5, -0.5) + 0.5;

    return prevNDC;
}
```

### TAA ブレンディング

```hlsl
float4 TAAPS(float2 uv : TEXCOORD0) : SV_Target {
    float depth   = depthBuffer.Sample(pointSampler, uv).r;
    float2 prevUV = CalcReprojectedUV(uv, depth, prevViewProj, invViewProj);

    // Velocity ベースの再投影（モーションブラーベクターがある場合）
    float2 velocity = velocityBuffer.Sample(pointSampler, uv).rg;
    float2 prevUV2  = uv - velocity;

    // 履歴サンプリング
    float3 history = historyBuffer.Sample(linearSampler, prevUV).rgb;
    float3 current = currentFrame.Sample(linearSampler, uv).rgb;

    // 履歴のクリッピング（ゴースト防止）
    // AABB クリッピング: 近傍9サンプルのバウンディングボックスで履歴をクリップ
    float3 minColor = float3(1e10, 1e10, 1e10);
    float3 maxColor = float3(-1e10, -1e10, -1e10);
    [unroll]
    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            float3 s = currentFrame.Sample(pointSampler, uv + float2(x, y) / screenSize).rgb;
            minColor = min(minColor, s);
            maxColor = max(maxColor, s);
        }
    }
    history = clamp(history, minColor, maxColor);

    // ブレンド（通常 0.1 前後: 10% 現在 + 90% 履歴）
    float blendFactor = 0.1;
    // 画面外のピクセルは履歴なし
    if (any(prevUV < 0.0) || any(prevUV > 1.0))
        blendFactor = 1.0;

    return float4(lerp(history, current, blendFactor), 1.0);
}
```

### TAA の問題点と対策

| 問題 | 対策 |
|---|---|
| ゴースト（残像）| AABB クリッピング、Velocity Buffer |
| モーションブラー | シャープニングフィルター、Catmull-Rom 補間 |
| ジッターによるブレ | サブピクセル精度での注意 |
| テキストのぼけ | UI を TAA の前に描画 |

---

## Checkerboard Rendering

奇数/偶数フレームで互い違いのピクセルを描画し、TAA で補完する。
実質 50% の解像度で 1920×1080 相当の品質。
PS4・Xbox One 世代のゲームで多用。

---

## DLSS / FSR との関係

| 技術 | 種別 | 対応 |
|---|---|---|
| DLSS 3 | AI アップスケーリング（NVIDIA） | Turing 以降、`NVIDIAGameWorks.sdk` |
| FSR 2/3 | 時間的アップスケーリング（AMD） | オープンソース（MIT）、DX11/12/Vulkan |
| XeSS | AI アップスケーリング（Intel） | オープンソース対応あり |

TAA の発展形として理解するとよい（Jitter + 時間統合が基礎）。

---

## 関連ドキュメント
- [11-post-processing.md](11-post-processing.md) - ポストプロセスチェーン
- [../roadmap/04-post-process.md](../roadmap/04-post-process.md) - TAA 実装フェーズ
