# Roadmap: Phase 10 - VFX Rendering（ビジュアルエフェクト）

**プロジェクト**: `D:/dev/shader/vfx-rendering/`
**API**: Direct3D 12
**目標**: ゲームで必要な全種類のビジュアルエフェクトをシェーダーで実装する

---

## 習得する概念

| 概念 | 関連ドキュメント |
|---|---|
| GPU パーティクル | [06-compute-effects.md](06-compute-effects.md) |
| Compute Shader（シミュレーション） | [15-compute-shaders.md](../concepts/15-compute-shaders.md) |
| 透明・半透明 | [14-advanced-rendering.md](../concepts/14-advanced-rendering.md) |
| プロシージャルノイズ（炎・煙） | [17-procedural-noise.md](../concepts/17-procedural-noise.md) |

---

## フェーズ分け

### フェーズ 10-1: 高度な GPU パーティクルシステム

Phase 6 の基礎を拡張し、Niagara 相当の表現力を実現する。

**実装項目**:
- 複数のエミッタタイプ（ポイント・スフィア・メッシュサーフェス）
- コリジョン（深度バッファとの交差判定 = Screen Space Collision）
- パーティクル間の引力・斥力（N 体シミュレーション近似）
- Curl Noise ベースの乱流（煙・霧）
- パーティクルからのライト放射（点光源への追加）

```hlsl
// Curl Noise（回転する流れ場）
float3 CurlNoise(float3 p, float time) {
    float eps = 0.01;
    // FBM の偏微分
    float3 dx = float3(eps, 0, 0), dy = float3(0, eps, 0), dz = float3(0, 0, eps);

    // dF/dy - dF/dz（各軸の curl）
    float3 curl;
    curl.x = (FBM(p + dy, time) - FBM(p - dy, time)
             - FBM(p + dz, time) + FBM(p - dz, time)) / (2.0 * eps);
    curl.y = (FBM(p + dz, time) - FBM(p - dz, time)
             - FBM(p + dx, time) + FBM(p - dx, time)) / (2.0 * eps);
    curl.z = (FBM(p + dx, time) - FBM(p - dx, time)
             - FBM(p + dy, time) + FBM(p - dy, time)) / (2.0 * eps);
    return curl;
}

[numthreads(256, 1, 1)]
void SmokeUpdateCS(uint id : SV_DispatchThreadID) {
    Particle p = particles[id];
    // Curl Noise で乱流
    float3 turbulence = CurlNoise(p.position * noiseScale, time) * turbulenceStrength;
    p.velocity += (gravity + turbulence) * deltaTime;
    p.position += p.velocity * deltaTime;

    // Screen Space コリジョン
    float4 clipPos  = mul(viewProj, float4(p.position, 1));
    float2 screenUV = clipPos.xy / clipPos.w * float2(0.5, -0.5) + 0.5;
    if (all(screenUV >= 0) && all(screenUV <= 1)) {
        float sceneDepth = LinearizeDepth(depthBuffer.SampleLevel(samp, screenUV, 0).r);
        float partDepth  = LinearizeDepth(clipPos.z / clipPos.w);
        if (partDepth > sceneDepth) {
            p.velocity = reflect(p.velocity, GetDepthNormal(screenUV)) * 0.3;
            p.position.y = ReconstructWorldPos(screenUV, sceneDepth).y + 0.01;
        }
    }
    particles[id] = p;
}
```

---

### フェーズ 10-2: フリップブック・テクスチャシートアニメーション

**実装項目**:
- テクスチャアトラス（8×8 など）から UV を計算
- 隣接フレームのクロスフェード（`lerp` によるモーションブラー軽減）
- 法線マップ対応フリップブック
- 炎・爆発・魔法エフェクト向けのベイクドシミュレーション再生

```hlsl
// フリップブック UV 計算
float4 SampleFlipbook(Texture2D tex, SamplerState samp, float2 uv,
                       float frame, float2 gridSize) {
    float totalFrames = gridSize.x * gridSize.y;
    float frameA = floor(frame) % totalFrames;
    float frameB = (frameA + 1.0) % totalFrames;
    float blend  = frac(frame);

    float2 tileSize = 1.0 / gridSize;
    float2 uvA = (float2(fmod(frameA, gridSize.x), floor(frameA / gridSize.x)) + uv) * tileSize;
    float2 uvB = (float2(fmod(frameB, gridSize.x), floor(frameB / gridSize.x)) + uv) * tileSize;

    return lerp(tex.Sample(samp, uvA), tex.Sample(samp, uvB), blend);
}
```

---

### フェーズ 10-3: ソフトパーティクル

**実装項目**:
- 深度バッファとパーティクルの深度差でエッジをフェードアウト
- カメラとパーティクルの Near Fade（近距離での消滅）

```hlsl
float4 SoftParticlePS(VSOutput input) : SV_Target {
    float4 color = albedoTex.Sample(samp, input.uv) * input.color;

    // Soft Particle フェード
    float sceneDepth   = LinearizeDepth(depthBuffer.Sample(samp, input.screenUV).r);
    float partDepth    = LinearizeDepth(input.screenPos.z / input.screenPos.w);
    float softFade     = saturate((sceneDepth - partDepth) / softFadeDistance);

    // Near Fade（カメラに近すぎると消える）
    float nearFade     = saturate((partDepth - nearFadeStart) / nearFadeRange);

    color.a *= softFade * nearFade;
    return color;
}
```

---

### フェーズ 10-4: 炎シェーダー

**実装項目**:
- 体積ノイズ（3D FBM）を Ray March してボリューム炎を描画
- 炎の色グラデーション（黒→赤→オレンジ→黄→白）を温度で制御
- 揺らめき: ドメインワープ + 時間スクロール
- 放射ライトへの変換（Compute Shader で点光源強度を更新）
- 板ポリ炎（テクスチャシート + ノイズ Mask）と体積炎の比較

```hlsl
// 炎の体積密度関数
float FlameSDensity(float3 pos, float time) {
    // 炎の芯に近いほど密度が高い
    float dist = length(pos.xz) / flameRadius;
    float height = pos.y / flameHeight;

    // 高さで炎が細くなる
    if (height > 1.0 || dist > 1.0 - height * 0.8) return 0.0;

    // 乱流ノイズ（ドメインワープ）
    float3 warpedPos = pos + float3(
        FBM(pos * 2.1 + float3(0, -time * 0.5, 0)),
        FBM(pos * 1.8 + float3(0, -time * 0.4, 0)),
        0.0
    ) * 0.3;
    float noise = FBM(warpedPos * 3.0 + float3(0, -time, 0));

    return saturate(noise * (1.0 - height) * (1.0 - dist));
}

// 温度から色へのマッピング
float3 FlameColor(float temperature) {
    float3 black  = float3(0.0, 0.0, 0.0);
    float3 red    = float3(0.8, 0.1, 0.0);
    float3 orange = float3(1.0, 0.5, 0.0);
    float3 yellow = float3(1.0, 1.0, 0.0);
    float3 white  = float3(1.0, 1.0, 1.0);

    if (temperature < 0.25) return lerp(black, red, temperature / 0.25);
    if (temperature < 0.5)  return lerp(red, orange, (temperature - 0.25) / 0.25);
    if (temperature < 0.75) return lerp(orange, yellow, (temperature - 0.5) / 0.25);
    return lerp(yellow, white, (temperature - 0.75) / 0.25);
}
```

---

### フェーズ 10-5: 爆発・破壊エフェクト

**実装項目**:
- Voronoi フラクチャ（SDF でメッシュを分割）
- 破片の物理シミュレーション（GPU Rigidbody 近似）
- ショックウェーブ（法線マップを使った UV 歪み）
- スクリーンスペース歪み（衝撃波の屈折バッファ）

```hlsl
// シックウェーブ歪み
float4 ShockwavePS(float2 uv : TEXCOORD0) : SV_Target {
    float2 center  = shockwavePos;
    float  dist    = length(uv - center);
    float  wave    = sin(dist * waveFrequency - time * waveSpeed) * waveStrength;

    // 衝撃波リング（距離で制限）
    float  mask    = smoothstep(waveRadius + 0.05, waveRadius, dist)
                   * smoothstep(waveRadius - 0.1, waveRadius, dist);
    float2 offset  = normalize(uv - center) * wave * mask;

    return sceneBuffer.Sample(samp, uv + offset);
}
```

---

### フェーズ 10-6: Deferred Decals（デカール）

**実装項目**:
- デカールボックスジオメトリの描画
- G-Buffer の Albedo・Normal・Roughness を上書き
- ステンシルでオーバーラップ制御
- 動的デカール（弾痕・血痕・汚れ）の管理

```hlsl
// Deferred Decal PS（G-Buffer を読み書き）
struct DecalOut {
    float4 albedo   : SV_Target0;
    float4 normal   : SV_Target1;
};

DecalOut DecalPS(float4 screenPos : SV_Position) {
    float2 uv = screenPos.xy / screenSize;

    // G-Buffer から深度を読んで World Position を復元
    float  depth    = depthBuffer.Sample(pointSamp, uv).r;
    float3 worldPos = ReconstructWorldPos(uv, depth, invViewProj);

    // デカールのローカル空間に変換
    float3 decalPos = mul(invDecalWorld, float4(worldPos, 1)).xyz;
    if (any(abs(decalPos) > 0.5)) discard; // ボックス外

    float2 decalUV = decalPos.xz + 0.5;
    float4 decalAlbedo = decalTex.Sample(samp, decalUV);
    clip(decalAlbedo.a - 0.01);

    DecalOut output;
    output.albedo = decalAlbedo;
    output.normal = float4(SampleDecalNormal(decalUV), 1.0);
    return output;
}
```

---

### フェーズ 10-7: レンズフレア・光学エフェクト

**実装項目**:
- Lens Flare: 光源の画面内位置から同心円 / 六角形 / ゴーストを配置
- Lens Dirt（汚れレンズ効果）: Bloom にマスクテクスチャを乗算
- 光源の遮蔽判定（Occlusion Query または深度サンプリング）
- Chromatic Aberration（収差）: RGB チャンネルを別々にサンプリング

```hlsl
// Chromatic Aberration
float4 ChromaticAberrationPS(float2 uv : TEXCOORD0) : SV_Target {
    float  strength = aberrationStrength * length(uv - 0.5); // 周辺ほど強い
    float2 dir      = normalize(uv - 0.5);

    float r = sceneBuffer.Sample(samp, uv + dir * strength * 1.0).r;
    float g = sceneBuffer.Sample(samp, uv).g;
    float b = sceneBuffer.Sample(samp, uv - dir * strength * 1.0).b;
    return float4(r, g, b, 1.0);
}
```

---

### フェーズ 10-8: スクリーンスペース UI エフェクト

**実装項目**:
- HUD のグロー（Bloom のマスク付き版）
- 被弾インジケータ（エッジに赤いビネット）
- スコープ / スナイパーサイト（レンダーターゲット内レンダー）
- インタラクティブ液体（表面張力・波紋シミュレーション）

---

## ファイル構成（完成時）

```
vfx-rendering/
├── CMakeLists.txt
├── src/
│   ├── VFXSystem.cpp/.h         ← エフェクト管理・プール
│   ├── ParticleSystemAdvanced.cpp/.h
│   ├── FlameRenderer.cpp/.h
│   ├── DecalManager.cpp/.h
│   └── LensFlare.cpp/.h
└── shaders/
    ├── particle_vs.hlsl / particle_ps.hlsl
    ├── soft_particle_ps.hlsl
    ├── flame_ps.hlsl            ← Ray March ボリューム炎
    ├── shockwave_ps.hlsl
    ├── decal_ps.hlsl
    ├── lens_flare_ps.hlsl
    └── chromatic_aberration_ps.hlsl
```

---

## 確認チェックリスト

- [ ] ソフトパーティクルが地面とのエッジで自然にフェードする
- [ ] Curl Noise で煙が自然に対流する
- [ ] 体積炎が視点移動中も立体的に見える
- [ ] デカールが地面の法線マップに沿って貼り付けられる
- [ ] Chromatic Aberration が画面中央では目立たず、周辺でかかる

---

## 関連ドキュメント
- [09-environment-rendering.md](09-environment-rendering.md) - 前フェーズ
- [11-render-graph.md](11-render-graph.md) - 次フェーズ
- [../concepts/17-procedural-noise.md](../concepts/17-procedural-noise.md)
- [../concepts/14-advanced-rendering.md](../concepts/14-advanced-rendering.md)
