# 05 - Texturing & Sampling（テクスチャリング・サンプリング）

## UV 座標と属性補間

UV 座標は各頂点に割り当てられたテクスチャ座標で、ラスタライザが補間する。

```
D3D の UV 規約:
U: 左→右 (0.0 → 1.0)
V: 上→下 (0.0 → 1.0)  ← OpenGL は逆（下→上）

範囲外の座標はアドレスモードで制御
```

```hlsl
// VS でUV を渡す
output.uv = input.texCoord; // そのまま渡す

// または変換（スクロール・タイリング）
output.uv = input.texCoord * tiling + offset;
```

---

## テクスチャフィルタリング

### Nearest Neighbor（最近傍）
- ピクセル中心に最も近いテクセルを使用
- **特徴**: ブロック状・クリスプ（ピクセルアート向き）
- `D3D11_FILTER_MIN_MAG_MIP_POINT`

### Bilinear（バイリニア）
- 隣接 2×2 テクセルを線形補間
- **特徴**: 滑らか。ミップ境界でポッピングあり
- `D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT`

### Trilinear（トリリニア）
- バイリニア + ミップレベル間も補間
- **特徴**: ミップ境界が滑らか。Bilinear より少し重い
- `D3D11_FILTER_MIN_MAG_MIP_LINEAR`

### Anisotropic（異方性フィルタリング）
- 斜め方向のテクスチャの鮮明さを改善
- **特徴**: 斜め面でもシャープ。現代GPU ではほぼコスト無し
- `D3D11_FILTER_ANISOTROPIC`、`MaxAnisotropy = 16`（推奨）

```cpp
D3D11_SAMPLER_DESC samplerDesc = {};
samplerDesc.Filter         = D3D11_FILTER_ANISOTROPIC;
samplerDesc.AddressU       = D3D11_TEXTURE_ADDRESS_WRAP;
samplerDesc.AddressV       = D3D11_TEXTURE_ADDRESS_WRAP;
samplerDesc.AddressW       = D3D11_TEXTURE_ADDRESS_WRAP;
samplerDesc.MaxAnisotropy  = 16;
samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
samplerDesc.MinLOD         = 0;
samplerDesc.MaxLOD         = D3D11_FLOAT32_MAX;
```

---

## ミップマッピング

テクスチャを段階的に縮小したバージョンを事前計算。

```
Mip 0: 1024×1024 (オリジナル)
Mip 1:  512×512
Mip 2:  256×256
...
Mip 10:    1×1
```

**なぜ必要か**:
- 遠距離のオブジェクトが高解像度テクスチャを使うと**エイリアシング**が発生
- ミップマップにより適切な解像度を自動選択
- **メモリ**: 元テクスチャの 33% 増（1/4 + 1/16 + ... = 1/3 の等比数列）

```cpp
// D3D11 でミップマップ生成
context->GenerateMips(srv); // D3D11_BIND_RENDER_TARGET | SHADER_RESOURCE 必須
```

```hlsl
// ミップレベルを手動指定
float4 color = tex.SampleLevel(samp, uv, 2.0); // Mip 2 を使用

// ミップレベルを計算（LOD バイアス）
float4 color = tex.SampleBias(samp, uv, -1.0); // シャープ方向にバイアス
```

---

## サンプラーステート

### AddressMode（範囲外 UV の扱い）

| モード | 動作 | 用途 |
|---|---|---|
| `WRAP` | タイリング（繰り返し） | 地面・壁など |
| `MIRROR` | 鏡面タイリング | シームレスなパターン |
| `CLAMP` | 端のピクセルを引き伸ばし | UI・スプライト |
| `BORDER` | 境界色を使用 | Shadow Map（0 or 1 を設定） |
| `MIRROR_ONCE` | 1 回だけミラー後 CLAMP | |

```hlsl
// Shadow Map 用サンプラー（境界を白=1 に設定）
D3D11_SAMPLER_DESC shadowSamplerDesc = {};
shadowSamplerDesc.Filter         = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
shadowSamplerDesc.AddressU       = D3D11_TEXTURE_ADDRESS_BORDER;
shadowSamplerDesc.AddressV       = D3D11_TEXTURE_ADDRESS_BORDER;
shadowSamplerDesc.AddressW       = D3D11_TEXTURE_ADDRESS_BORDER;
shadowSamplerDesc.BorderColor[0] = 1.0f; // 全て白
shadowSamplerDesc.BorderColor[1] = 1.0f;
shadowSamplerDesc.BorderColor[2] = 1.0f;
shadowSamplerDesc.BorderColor[3] = 1.0f;
shadowSamplerDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
```

---

## テクスチャ型

### Texture2D（最も一般的）
```hlsl
Texture2D<float4> albedo : register(t0);
float4 color = albedo.Sample(samp, uv);
```

### TextureCube（Cube Map）
```hlsl
TextureCube envMap : register(t1);
float3 reflectDir = reflect(-viewDir, normal);
float4 envColor = envMap.Sample(samp, reflectDir);
```

**用途**: スカイボックス、環境反射、点光源シャドウマップ（Omnidirectional）

### Texture3D（ボリュームテクスチャ）
```hlsl
Texture3D<float> volumeData : register(t2);
float3 uvw = worldPos / volumeSize;
float density = volumeData.Sample(samp, uvw);
```

**用途**: ボリューメトリックフォグ、クラウド、3D LUT（Color Grading）

### Texture2DArray
```hlsl
Texture2DArray cascadedShadowMaps : register(t3);
float depth = cascadedShadowMaps.Sample(samp, float3(shadowUV, cascadeIndex));
```

**用途**: CSM（Cascaded Shadow Maps）、スプライトアニメーション

---

## PBR テクスチャマップ

### 標準 PBR セット

| マップ | チャンネル | 内容 |
|---|---|---|
| Albedo（Base Color） | RGB | 拡散反射色（金属では反射色） |
| Normal | RGB | タンジェント空間法線（青みがかった画像） |
| Metalness | R | 金属度（0=非金属, 1=金属） |
| Roughness | R | 粗さ（0=鏡面, 1=完全拡散） |
| AO（Ambient Occlusion） | R | ベイクされた環境光遮蔽 |
| Emissive | RGB | 自発光色 |

### テクスチャパッキング（バンドル最適化）
```
ORM パック（Unreal Engine 標準）:
  R: Occlusion
  G: Roughness
  B: Metalness

ARM パック:
  R: AO
  G: Roughness
  B: Metalness
```

```hlsl
// ORM テクスチャのサンプリング
float3 orm = ormTex.Sample(samp, uv).rgb;
float ao        = orm.r;
float roughness = orm.g;
float metalness = orm.b;
```

---

## ノーマルマップの正しい読み込み

```hlsl
// サンプリング後に [-1, 1] に変換
float3 tangentNormal = normalMap.Sample(samp, uv).rgb * 2.0 - 1.0;

// BC5 圧縮の場合（XY のみ格納、Z を復元）
float2 xy = normalMap.Sample(samp, uv).rg * 2.0 - 1.0;
float z = sqrt(max(0.0, 1.0 - dot(xy, xy))); // 単位球面上の Z を復元
float3 tangentNormal = float3(xy, z);
```

### sRGB vs Linear
| テクスチャ | 色空間 | 理由 |
|---|---|---|
| Albedo | sRGB（ガンマ） | アーティストが見た通りの色 |
| Normal | Linear | ベクトル値なのでガンマ補正不要 |
| Metalness/Roughness | Linear | 物理値なのでガンマ補正不要 |
| Emissive | sRGB | 表示色として定義 |

D3D11 では `DXGI_FORMAT_R8G8B8A8_UNORM_SRGB` と `_UNORM` を正しく区別すること。

---

## 関連ドキュメント
- [04-normal-handling.md](04-normal-handling.md) - TBN とノーマルマップ変換
- [06-lighting-models.md](06-lighting-models.md) - テクスチャをライティングで使用
- [08-pbr-theory.md](08-pbr-theory.md) - PBR テクスチャの物理的意味
- [11-post-processing.md](11-post-processing.md) - 3D LUT Color Grading
