# Roadmap: Phase 4 - Post-Processing

**プロジェクト**: `D:/dev/shader/post-process/`
**API**: Direct3D 11 / 12
**目標**: フルスクリーンパスと HDR パイプラインを構築する

---

## 習得する概念

| 概念 | 関連ドキュメント |
|---|---|
| RTT・フルスクリーンパス | [11-post-processing.md](../concepts/11-post-processing.md) |
| アンチエイリアシング | [12-anti-aliasing.md](../concepts/12-anti-aliasing.md) |
| SSAO（発展） | [13-ambient-occlusion.md](../concepts/13-ambient-occlusion.md) |

---

## フェーズ分け

### フェーズ 4-1: RTT フレームワーク（MRT 対応）

**実装項目**:
- HDR レンダーターゲット（`DXGI_FORMAT_R16G16B16A16_FLOAT`）
- 深度テクスチャ（`R24G8_TYPELESS`）
- Velocity バッファ（`R16G16_FLOAT`）
- フルスクリーン三角形（頂点バッファ不要）
- ポストプロセスパス基底クラス

```cpp
// HDR + Depth + Velocity の MRT セットアップ
ID3D11RenderTargetView* rtvs[2] = { hdrRTV, velocityRTV };
context->OMSetRenderTargets(2, rtvs, depthDSV);
```

**フルスクリーン三角形（頂点なし）**:
```hlsl
void FullscreenVS(
    uint id : SV_VertexID,
    out float4 pos : SV_Position,
    out float2 uv  : TEXCOORD0
) {
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos  = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}
// context->Draw(3, 0) で描画（IA のバッファ設定不要）
```

---

### フェーズ 4-2: Tone Mapping（ACES Filmic）+ Gamma Correction

**実装項目**:
- ACES Filmic トーンマッピング（Stephen Hill フィット）
- Linear → sRGB 変換（ガンマ補正）
- 露出（Exposure）スライダー
- 複数の Tone Mapper を比較できるデバッグ切り替え

```hlsl
float4 ToneMappingPS(float2 uv : TEXCOORD0) : SV_Target {
    float3 hdr = hdrBuffer.Sample(samp, uv).rgb;
    hdr *= exposure;

    // ACES Filmic
    float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    float3 ldr = saturate((hdr * (a * hdr + b)) / (hdr * (c * hdr + d) + e));

    // Linear → sRGB（手動）
    // ※ DXGI_FORMAT_R8G8B8A8_UNORM_SRGB スワップチェーンを使えば不要
    ldr = pow(max(ldr, 0.0), 1.0 / 2.2);

    return float4(ldr, 1.0);
}
```

---

### フェーズ 4-3: Bloom（Dual Kawase ブラー）

**実装項目**:
- 輝度抽出パス（閾値 + 輝度計算）
- ダウンサンプリング x4（1/2・1/4・1/8・1/16）
- アップサンプリング（逆順）
- 元画像への加算ブレンド
- Bloom 強度・閾値のパラメータ

```
パイプライン:
  [HDR Buffer]
    → ThresholdPass（輝度抽出）
    → Downsample×N
    → Upsample×N
    → Composite（元画像 + Bloom * intensity）
    → ToneMapping
```

---

### フェーズ 4-4: SSAO 統合

**実装項目**:
- Phase 2 の SSAO を HDR パイプラインに統合
- AO をライティング計算の前に適用
- `color *= ssao` のタイミングを正確に管理

---

### フェーズ 4-5: FXAA

**実装項目**:
- Timothy Lottes の FXAA 3.11 シェーダーを組み込む
- Tone Mapping 後（LDR）に適用
- Quality プリセット（Performance / Quality / Extreme）

```
適用順序: ToneMapping → FXAA → Color Grading → Output
```

---

### フェーズ 4-6: TAA（Jitter + Reprojection + 履歴バッファ）

**実装項目**:
- Halton シーケンスでジッター
- 毎フレームのプロジェクション行列オフセット
- Velocity Buffer（Geometry Pass で生成）
- 履歴バッファ（前フレームの TAA 出力）
- AABB クリッピング（ゴースト防止）
- ブレンドファクター（0.1 前後）

```hlsl
// Halton シーケンス
float HaltonSequence(int index, int base) {
    float result = 0.0;
    float f = 1.0;
    int   i = index;
    while (i > 0) {
        f = f / float(base);
        result += f * float(i % base);
        i = int(floor(float(i) / float(base)));
    }
    return result;
}

// ジッターオフセット（16フレームサイクル）
float2 jitter = float2(
    HaltonSequence(frameIndex % 16 + 1, 2),
    HaltonSequence(frameIndex % 16 + 1, 3)
) * 2.0 - 1.0;
jitter /= float2(screenWidth, screenHeight);
```

---

### フェーズ 4-7: Depth of Field（CoC ベース）

**実装項目**:
- Circle of Confusion の計算（焦点距離・絞り）
- CoC マップの生成
- CoC に応じたカーネルサイズでのブラー
- 前景（Focus より手前）と背景（Focus より後ろ）の分離処理

---

### フェーズ 4-8: Motion Blur（Velocity Buffer）

**実装項目**:
- Velocity Buffer に現在フレームと前フレームのクリップ座標の差を書き込む
- Motion Blur パスでサンプリング方向と長さを決定
- カメラ Motion Blur とオブジェクト Motion Blur の統合

---

### フェーズ 4-9: Color Grading（3D LUT）

**実装項目**:
- 32×32×32 の 3D LUT テクスチャ作成
- Identity LUT から始める（変更なし）
- 外部 .cube ファイルからの LUT 読み込み（RGBEライブラリ or 自作）
- LUT のバイリニアサンプリング（テクセル中心補正）

---

### フェーズ 4-10（発展）: Custom Temporal Upscaling の基礎

- 半解像度でシーンをレンダリング
- TAA の Jitter と履歴バッファを活用して解像度を回復
- FSR 2 の基本原理の理解

---

## ポストプロセスパイプライン設計

```cpp
class PostProcessPipeline {
    // 各パスをキューに追加して順番に実行
    void AddPass(IPostProcessPass* pass);
    void Execute(ID3D11DeviceContext* ctx, Texture2D* input, Texture2D* output);

    // 内部ダブルバッファリング（ping-pong）
    Texture2D pingBuffer, pongBuffer;
};

// 使用例
pipeline.AddPass(new SSAOPass());
pipeline.AddPass(new BloomPass());
pipeline.AddPass(new MotionBlurPass());
pipeline.AddPass(new DofPass());
pipeline.AddPass(new ToneMappingPass());
pipeline.AddPass(new TAAPass());
pipeline.AddPass(new FXAAPass());
pipeline.AddPass(new ColorGradingPass());
```

---

## ファイル構成（完成時）

```
post-process/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── D3DApp.cpp/.h
│   ├── PostProcessPipeline.cpp/.h
│   ├── passes/
│   │   ├── SSAOPass.cpp/.h
│   │   ├── BloomPass.cpp/.h
│   │   ├── ToneMappingPass.cpp/.h
│   │   ├── TAAPass.cpp/.h
│   │   ├── FXAAPass.cpp/.h
│   │   ├── DoFPass.cpp/.h
│   │   ├── MotionBlurPass.cpp/.h
│   │   └── ColorGradingPass.cpp/.h
└── shaders/
    ├── fullscreen_vs.hlsl     ← 全パス共通 VS
    ├── ssao_ps.hlsl
    ├── bloom_threshold_ps.hlsl
    ├── bloom_downsample_ps.hlsl
    ├── bloom_upsample_ps.hlsl
    ├── tonemapping_ps.hlsl
    ├── taa_ps.hlsl
    ├── fxaa_ps.hlsl
    ├── dof_ps.hlsl
    ├── motion_blur_ps.hlsl
    └── color_grading_ps.hlsl
```

---

## 確認チェックリスト

- [ ] HDR バッファで明るいハイライトが 1.0 を超えることを確認（RenderDoc で確認）
- [ ] Tone Mapping で明るいシーンが破綻しないことを確認
- [ ] Bloom の光源が自然に滲む
- [ ] TAA でジャギーが減少し、動くオブジェクトにゴーストがない
- [ ] Velocity Buffer が正しく出力されている（RenderDoc での可視化）
- [ ] Color Grading で LUT を変えると即座に見た目が変わる

---

## 関連ドキュメント
- [03-pbr.md](03-pbr.md) - 前フェーズ
- [../concepts/11-post-processing.md](../concepts/11-post-processing.md)
- [../concepts/12-anti-aliasing.md](../concepts/12-anti-aliasing.md)
- [../roadmap/overview.md](overview.md) - Phase 5 以降のロードマップ
