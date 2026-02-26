# Roadmap: Phase 8 - Character Rendering（キャラクターレンダリング）

**プロジェクト**: `D:/dev/shader/character-rendering/`
**API**: Direct3D 12
**目標**: 人体の各パーツ（肌・髪・眼・布）に最適化されたシェーダーを実装する

---

## 習得する概念

| 概念 | 関連ドキュメント |
|---|---|
| PBR BRDF の拡張 | [08-pbr-theory.md](../concepts/08-pbr-theory.md) |
| 法線・タンジェント | [04-normal-handling.md](../concepts/04-normal-handling.md) |
| GPU スキニング | [06-compute-effects.md](06-compute-effects.md) |
| RT シャドウ（高品質影） | [07-ray-tracing.md](07-ray-tracing.md) |

---

## フェーズ分け

### フェーズ 8-1: Pre-Integrated Skin Shading（簡易 SSS）

**目標**: リアルタイムで実用的な肌の表面下散乱を近似する。

**実装項目**:
- Pre-Integrated Skin BRDF テクスチャ（E. d'Eon & D. Luebke, 2007）
- 曲率（Curvature）の計算: `length(fwidth(N)) / length(fwidth(P))`
- `float2(NdotL * 0.5 + 0.5, curvature)` で 2D LUT をサンプリング
- 透過光（Translucency）: 薄い部分（耳・鼻翼）に裏面ライティングを追加

```hlsl
// Pre-Integrated LUT を使った肌シェーディング
float  curvature = saturate(length(fwidth(N)) / length(fwidth(worldPos)) * curvatureScale);
float2 sssUV     = float2(saturate(NdotL * 0.5 + 0.5), curvature);
float3 sssColor  = sssLUT.Sample(linearSamp, sssUV).rgb;

// 拡散ライティングを LUT で置き換え
float3 diffuse = lightColor * albedo * sssColor;

// 裏面透過（薄い組織の光透過）
float  backNdotL  = max(dot(-N, L), 0.0);
float3 translucency = backLight * albedo * backNdotL * subsurfaceColor * thickness;
```

---

### フェーズ 8-2: Separable SSS（高品質スクリーン空間 SSS）

**実装項目**:
- G-Buffer に肌の Diffuse 成分を別途格納（ShadingModel ID で識別）
- 横方向 + 縦方向のガウシアンブラー（RGB チャンネルで異なる kernel）
- カーネルの重みを事前計算（Burley SSS プロファイルに基づく）
- 深度・法線で境界を保持（バイラテラル）

```hlsl
// Jorge Jimenez の Separable SSS カーネル（6ガウシアンの和）
static const float3 SSS_KERNEL[NSAMPLES] = {
    // (weight.xyz, offset)
    float3(0.530605, 0.613514, 0.739601),  // 広い散乱（赤）
    float3(0.130501, 0.050680, 0.032000),  // 中間散乱（緑）
    // ...
};

float4 SSSBlurPS(float2 uv : TEXCOORD0) : SV_Target {
    float3 centerNormal = gbufNormal.Sample(samp, uv).xyz;
    float  centerDepth  = LinearizeDepth(gbufDepth.Sample(samp, uv).r);
    float3 color        = float3(0, 0, 0);

    [unroll]
    for (int i = 0; i < NSAMPLES; i++) {
        float2 offset      = float2(SSS_KERNEL[i].w, 0) * sssWidth * texelSize;
        float2 sampleUV    = uv + offset;
        float3 sampleColor = skinDiffuse.Sample(samp, sampleUV).rgb;
        float  sampleDepth = LinearizeDepth(gbufDepth.Sample(samp, sampleUV).r);

        // 深度差が大きい部分はブラーしない（輪郭部分の色滲み防止）
        float depthWeight = exp(-abs(sampleDepth - centerDepth) * 100.0);
        color += SSS_KERNEL[i].rgb * sampleColor * depthWeight;
    }
    return float4(color, 1.0);
}
```

---

### フェーズ 8-3: ヘアシェーダー（Hair Cards ベース）

ゲームでよく使われる Hair Cards（板ポリゴンの集合）アプローチ。

**実装項目**:
- Kajiya-Kay モデル（接線方向の拡散・鏡面反射）
- Marschner モデルの近似（R・TT・TRT ローブ）
- アルファカットアウト（Noise + Dithering で自然な境界）
- Anisotropic Specular（毛の方向に沿った楕円形ハイライト）
- 複数ハイライト（Primary + Secondary シフトで現実的な多層構造）

```hlsl
// Kajiya-Kay 拡散
float KajiyaDiffuse(float3 T, float3 L) {
    float sinTL = length(cross(T, L));
    return sinTL; // sqrt(1 - dot(T,L)^2) の近似
}

// Scheuermann の実用的な Anisotropic 鏡面
float KajiyaSpecular(float3 T, float3 V, float3 L, float shift, float exponent) {
    float3 H    = normalize(L + V);
    float  TdotH = dot(T, H);
    float  sinTH = sqrt(max(0.0, 1.0 - TdotH * TdotH));
    return pow(sinTH, exponent) * max(0.0, sign(TdotH + shift));
}

float4 HairPS(VSOutput input) : SV_Target {
    float3 T = normalize(input.tangent); // 毛流れ方向
    float3 V = normalize(cameraPos - input.worldPos);
    float3 L = normalize(lightPos - input.worldPos);
    float3 N = normalize(input.normal);

    // テクスチャからシフト値を取得（ハイライト位置を乱す）
    float shiftAmount = shiftTex.Sample(samp, input.uv).r - 0.5;
    float3 T1 = ShiftTangent(T, N, primaryShift  + shiftAmount);
    float3 T2 = ShiftTangent(T, N, secondaryShift + shiftAmount);

    float3 diffuse   = hairColor * KajiyaDiffuse(T, L);
    float3 spec1     = primaryColor   * KajiyaSpecular(T1, V, L, primaryShift,   primaryExp);
    float3 spec2     = secondaryColor * KajiyaSpecular(T2, V, L, secondaryShift, secondaryExp);

    float alpha = alphaTex.Sample(samp, input.uv).r;
    clip(alpha - 0.5);

    return float4((diffuse + spec1 + spec2) * lightColor, alpha);
}
```

---

### フェーズ 8-4: 眼球シェーダー

**実装項目**:
- 虹彩（Iris）の視差マッピング（Parallax Cornea Effect）
- 角膜（Cornea）のシャープな鏡面反射（低 Roughness の PBR）
- 強膜（Sclera）のわずかな SSS（赤みがかった血管のにじみ）
- Limbal Ring（虹彩の縁の暗い輪）
- Pupil Dilation（瞳孔の拡大縮小、動的 UV スケール）

```hlsl
float4 EyePS(VSOutput input) : SV_Target {
    float2 uv = input.uv;

    // 視差オフセット（角膜の屈折で虹彩が手前に見える）
    float3 V      = normalize(cameraPos - input.worldPos);
    float  ior    = 1.336; // 房水の屈折率
    float2 offset = (V.xy / V.z) * corneaDepth / ior;
    float2 irisUV = uv + offset;

    float3 irisColor = irisTex.Sample(samp, irisUV).rgb;

    // Limbal Ring（瞳孔外縁の暗い輪）
    float dist     = length(uv - 0.5) * 2.0;
    float limbal   = 1.0 - smoothstep(0.9, 1.0, dist);
    irisColor     *= limbal;

    // 強膜の PBR + わずかな SSS
    float3 scleraColor = scleraTex.Sample(samp, uv).rgb;

    // マスクで虹彩と強膜を合成
    float irisMask = step(dist, irisMaskRadius);
    float3 finalColor = lerp(scleraColor, irisColor, irisMask);

    // 角膜のシャープ鏡面（完全な PBR Specular）
    float3 corneaSpec = CookTorranceSpecular(input.worldNormal, V, L, 0.04, 0.02);
    finalColor += corneaSpec;

    return float4(finalColor, 1.0);
}
```

---

### フェーズ 8-5: 布シェーダー（Cloth Shading）

**実装項目**:
- Ashikhmin-Shirley BRDF（異方性）: ベルベット・サテンの近似
- Sheen（布のグレージングハイライト）: Disney の Sheen パラメータ
- Cross-Polarization（布表面の後方散乱）
- 布のノーマルマップ（織りパターン）との組み合わせ

```hlsl
// Charlie Sheen（UE4 で使われる Cloth 用 NDF）
float D_Charlie(float NdotH, float roughness) {
    float invAlpha  = 1.0 / (roughness * roughness);
    float cos2h     = NdotH * NdotH;
    float sin2h     = max(1.0 - cos2h, 0.0078125);
    return (2.0 + invAlpha) * pow(sin2h, invAlpha * 0.5) / (2.0 * PI);
}

// Cloth Shading
float3 ClothBRDF(float3 N, float3 V, float3 L, float3 albedo, float roughness) {
    float3 H    = normalize(L + V);
    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float NdotV = max(dot(N, V), 0.0);

    float  D    = D_Charlie(NdotH, roughness);
    float  V_   = 0.25 / max(NdotL + NdotV, 0.001); // Neubelt Visibility
    float3 F    = float3(0.04, 0.04, 0.04); // 布の F0

    float3 specular = (D * V_ * F) * NdotL;
    float3 diffuse  = albedo / PI * NdotL;

    return diffuse + specular;
}
```

---

### フェーズ 8-6: ファー（毛皮）シェーダー

**実装項目**:
- Shell Rendering: メッシュを N 層に押し出してアルファカットアウトで毛を表現
- Fin Rendering: エッジにポリゴンを生成して側面の毛を表現
- Flow Map: テクスチャで毛流れ方向をコントロール
- Wind Simulation: 頂点シェーダーで毛先を揺らす

```hlsl
// Shell Rendering VS: 法線方向に少しずつオフセット
VSOutput FurShellVS(VSInput input, uint instanceId : SV_InstanceID) {
    float  shellHeight = float(instanceId) / float(numShells); // 0〜1
    float3 offset      = input.normal * shellHeight * furLength;
    float3 worldPos    = mul(modelMatrix, float4(input.position + offset, 1)).xyz;

    output.uv          = input.uv;
    output.shellHeight = shellHeight;
    output.position    = mul(viewProj, float4(worldPos, 1));
    return output;
}

float4 FurShellPS(VSOutput input) : SV_Target {
    float2 furUV  = frac(input.uv * furDensity);
    float  alpha  = furNoise.Sample(samp, furUV).r;

    // 毛先に向かって細くなる
    clip(alpha - input.shellHeight);

    float3 color = furColor * (1.0 - input.shellHeight * 0.5);
    return float4(color, 1.0);
}
```

---

### フェーズ 8-7: LOD とパフォーマンス管理

**実装項目**:
- 距離に応じた Shading Model の切り替え
  - 近距離: フル SSS + Hair Cards
  - 中距離: Simplified SSS + Billboard Hair
  - 遠距離: 標準 PBR + テクスチャベース Hair
- Impostors（スプライト置き換え）
- キャラクター専用のシェーダー Permutation 管理

---

## ファイル構成（完成時）

```
character-rendering/
├── CMakeLists.txt
├── src/
│   ├── D3D12App.cpp/.h
│   ├── Character.cpp/.h         ← メッシュ・マテリアル・LOD 管理
│   ├── SSSPass.cpp/.h           ← Separable SSS
│   └── HairSystem.cpp/.h
└── shaders/
    ├── skin_gbuffer_ps.hlsl      ← SSS 用 Shading Model
    ├── sss_blur_h_ps.hlsl        ← 水平 SSS ブラー
    ├── sss_blur_v_ps.hlsl        ← 垂直 SSS ブラー
    ├── hair_vs.hlsl / hair_ps.hlsl
    ├── eye_ps.hlsl
    ├── cloth_ps.hlsl
    └── fur_shell_vs.hlsl / fur_shell_ps.hlsl
```

---

## 確認チェックリスト

- [ ] Pre-Integrated SSS で耳・鼻翼に赤みが出る
- [ ] Separable SSS で肌の赤みが隣のポリゴンに滲む（接触部分）
- [ ] 髪に 2 つのシフトしたハイライトが確認できる
- [ ] 眼球の角膜視差効果が視線角度に応じて変化する
- [ ] 布のベルベットが斜め方向に明るいグレージングを持つ
- [ ] LOD 切り替え時に目立つポッピングがない

---

## 関連ドキュメント
- [07-ray-tracing.md](07-ray-tracing.md) - 前フェーズ
- [09-environment-rendering.md](09-environment-rendering.md) - 次フェーズ
- [../concepts/08-pbr-theory.md](../concepts/08-pbr-theory.md)
- [../concepts/04-normal-handling.md](../concepts/04-normal-handling.md)
