# Roadmap: Phase 5 - Advanced Rendering

**プロジェクト**: `D:/dev/shader/advanced-rendering/`
**API**: Direct3D 12
**目標**: 大規模シーンに対応したレンダリングアーキテクチャを構築する

---

## 習得する概念

| 概念 | 関連ドキュメント |
|---|---|
| Deferred Shading・Forward+ | [14-advanced-rendering.md](../concepts/14-advanced-rendering.md) |
| Compute Shader（タイルカリング） | [15-compute-shaders.md](../concepts/15-compute-shaders.md) |
| SSR | [14-advanced-rendering.md](../concepts/14-advanced-rendering.md) |
| 最適化（Z-Prepass・Culling） | [16-optimization.md](../concepts/16-optimization.md) |
| 透過・デカール | [14-advanced-rendering.md](../concepts/14-advanced-rendering.md) |

---

## フェーズ分け

### フェーズ 5-1: D3D12 への移行

**実装項目**:
- `ID3D12Device` / `ID3D12CommandQueue` / `ID3D12CommandList` の基礎
- `IDXGISwapChain3` と Present フレームフリップ
- `ID3D12Fence` による CPU-GPU 同期（ダブルバッファリング）
- `D3D12_RESOURCE_BARRIER` の State 遷移（COMMON → RTV → SHADER_RESOURCE）
- `ID3D12DescriptorHeap`（RTV/DSV/CBV-SRV-UAV）
- Root Signature と PSO（Graphics Pipeline State Object）

```cpp
// D3D12 描画ループの最小構造
commandAllocator->Reset();
commandList->Reset(commandAllocator, pso);

// バリア: Present → RTV
auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
    renderTarget, D3D12_RESOURCE_STATE_PRESENT,
    D3D12_RESOURCE_STATE_RENDER_TARGET);
commandList->ResourceBarrier(1, &barrier);

commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
commandList->DrawIndexedInstanced(indexCount, 1, 0, 0, 0);

// バリア: RTV → Present
barrier = CD3DX12_RESOURCE_BARRIER::Transition(
    renderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET,
    D3D12_RESOURCE_STATE_PRESENT);
commandList->ResourceBarrier(1, &barrier);
commandList->Close();
```

**確認方法**: D3D12 で単色トライアングルが描画できる

---

### フェーズ 5-2: G-Buffer の設計と Geometry Pass

**実装項目**:
- MRT（Multiple Render Targets）の D3D12 セットアップ
- G-Buffer レイアウトの決定と実装
- Geometry Pass シェーダー（Albedo・Normal・ORM・Velocity を書き込み）
- Oct-Encoding（法線の 2 チャンネル圧縮）

```
推奨 G-Buffer レイアウト（ゲームエンジン向け）:
  RT0 R8G8B8A8_UNORM:    Albedo(RGB) + Roughness(A)
  RT1 R10G10B10A2_UNORM: WorldNormal(RGB, Oct-encoded) + Metalness(A)
  RT2 R16G16_FLOAT:      MotionVector
  DS  D32_FLOAT_S8X24_UINT: Depth + Stencil(ShadingModel ID)
```

```hlsl
// Oct-Encoding: 法線を 2ch に圧縮
float2 OctEncode(float3 n) {
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    float2 enc = n.z >= 0.0 ? n.xy : (1.0 - abs(n.yx)) * sign(n.xy);
    return enc * 0.5 + 0.5;
}
float3 OctDecode(float2 enc) {
    float2 f = enc * 2.0 - 1.0;
    float3 n = float3(f.xy, 1.0 - abs(f.x) - abs(f.y));
    float t = max(-n.z, 0.0);
    n.x += n.x >= 0.0 ? -t : t;
    n.y += n.y >= 0.0 ? -t : t;
    return normalize(n);
}
```

---

### フェーズ 5-3: Deferred Lighting Pass

**実装項目**:
- G-Buffer からの位置再構成（深度 + 逆 ViewProj）
- PBR BRDF（Phase 3 の成果）を Lighting Pass に組み込む
- ステンシルによる Shading Model 分岐（スタンダード / スキン / 発光）
- 指向性ライト・点光源・スポットライトのフルスクリーンパス
- Light Volume（点光源は球ジオメトリでステンシルマスク）

```hlsl
// Depth → World Position の再構成
float3 ReconstructWorldPos(float2 uv, float depth, float4x4 invViewProj) {
    float4 clip = float4(uv * 2.0 - 1.0, depth, 1.0);
    clip.y = -clip.y;
    float4 world = mul(invViewProj, clip);
    return world.xyz / world.w;
}
```

---

### フェーズ 5-4: Forward+（Tiled Light Culling）

**実装項目**:
- `TILE_SIZE = 16`（または 8）でスクリーンを分割
- Compute Shader でタイル毎の深度範囲を計算
- 視錐台タイルと光源 AABB の交差テスト（CS Atomic）
- Light Index List と Light Grid バッファの構築
- Forward Pass でタイルの光源リストを参照

```hlsl
// Light Grid: 各タイルの光源インデックス範囲
StructuredBuffer<uint2> lightGrid    : register(t10); // (count, offset)
StructuredBuffer<uint>  lightIndexList : register(t11);

float4 ForwardPlusPS(VSOutput input) : SV_Target {
    uint2 tileIdx    = (uint2)input.screenPos.xy / TILE_SIZE;
    uint  tileFlat   = tileIdx.y * numTilesX + tileIdx.x;
    uint  lightCount = lightGrid[tileFlat].x;
    uint  lightStart = lightGrid[tileFlat].y;

    float3 Lo = 0;
    for (uint i = 0; i < lightCount; i++) {
        uint lightIdx = lightIndexList[lightStart + i];
        Lo += EvaluatePBR(input, lights[lightIdx]);
    }
    return float4(Lo, 1.0);
}
```

**比較確認**: 100点光源でも Deferred と同等フレームレートが出ることを確認

---

### フェーズ 5-5: Clustered Shading（3D タイル）

**実装項目**:
- 深度方向を対数分割してクラスター生成（XY タイル × Z スライス）
- 各クラスターの AABB を事前計算（CS）
- 光源とクラスター AABB の交差テスト
- Cluster Key からインデックスリストを引く仕組み

```
クラスター数の例:
  XY: 32×18 タイル（1920×1080 を 60px 単位）
  Z:  24 スライス（対数分割 0.1m〜1000m）
  合計: 13824 クラスター
```

---

### フェーズ 5-6: Screen Space Reflections（SSR）

**実装項目**:
- View Space でのレイマーチング（Hi-Z Trace）
- Roughness によるコーンサンプリング（ブラー量決定）
- スクリーン外れ時の IBL フォールバック
- TAA との統合（時間的ノイズ削減）

---

### フェーズ 5-7: Volumetric Fog / God Rays

**実装項目**:
- フラスタムをボクセルグリッドに分割（Froxel: Frustum Voxel）
- 各 Froxel に散乱・吸収・放射輝度を蓄積（CS）
- Ray Marching で Froxel テクスチャをサンプリング
- シャドウマップとの統合（光シャフトの自己影）

```hlsl
// Froxel テクスチャ（View Space を 3D テクスチャに対応付け）
Texture3D<float4> froxelVolume : register(t5); // RGBA: 散乱RGB + 透過A
float4 SampleFroxel(float3 worldPos) {
    float3 viewPos = mul(viewMatrix, float4(worldPos, 1)).xyz;
    float  sliceZ  = log(viewPos.z / nearZ) / log(farZ / nearZ); // 対数Z
    float3 uvw     = float3(screenUV, sliceZ);
    return froxelVolume.Sample(linearSamp, uvw);
}
```

---

### フェーズ 5-8: Z-Prepass と Early-Z 最適化

**実装項目**:
- Z-Prepass（深度のみ書き込む軽量パス）
- Geometry Pass で `DepthFunc = EQUAL` + `DepthWriteMask = ZERO`
- フラスタムカリング（CPU）と Hi-Z オクルージョンカリング（GPU, CS）
- GPU 駆動描画（DrawIndirect）への橋渡し

---

### フェーズ 5-9: Deferred Decals

**実装項目**:
- デカールのボックスジオメトリを描画（Deferred Decal Pass）
- G-Buffer の Albedo・Normal・ORM を上書き
- ステンシルバッファでデカールの影響範囲を制限

---

### フェーズ 5-10: Order Independent Transparency（OIT）

**実装項目**:
- Weighted Blended OIT（Deferred との相性がよい近似手法）
- Accumulation バッファ（`RGBA16F`）と Reveal バッファ（`R16F`）
- Composite Pass での最終合成
- 不透明パスの後に透明パスを別途実行

---

## ファイル構成（完成時）

```
advanced-rendering/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── D3D12App.cpp/.h          ← D3D12 フレームワーク
│   ├── DescriptorHeap.cpp/.h    ← ディスクリプタ管理
│   ├── GBuffer.cpp/.h
│   ├── DeferredRenderer.cpp/.h
│   ├── ForwardPlusRenderer.cpp/.h
│   ├── LightCuller.cpp/.h       ← タイルカリング CS
│   ├── VolumetricFog.cpp/.h
│   └── SSR.cpp/.h
└── shaders/
    ├── common.hlsli
    ├── gbuffer_vs.hlsl / gbuffer_ps.hlsl
    ├── lighting_pass_ps.hlsl
    ├── light_culling_cs.hlsl
    ├── forward_plus_ps.hlsl
    ├── ssr_cs.hlsl
    ├── volumetric_fog_cs.hlsl
    ├── decal_ps.hlsl
    └── oit_composite_ps.hlsl
```

---

## 確認チェックリスト

- [ ] D3D12 バリアのステート遷移を PIX で確認した
- [ ] G-Buffer の各チャンネルを RenderDoc で個別に確認した
- [ ] 100 点光源のシーンで Deferred vs Forward+ のフレームレートを比較した
- [ ] SSR が Roughness に応じて正しくブラーされる
- [ ] Volumetric Fog のフラスタムが正しいカメラ範囲をカバーしている
- [ ] OIT で透明オブジェクトのソートなしに正しく合成される

---

## 関連ドキュメント
- [04-post-process.md](04-post-process.md) - 前フェーズ
- [../concepts/14-advanced-rendering.md](../concepts/14-advanced-rendering.md)
- [../concepts/15-compute-shaders.md](../concepts/15-compute-shaders.md)
- [06-compute-effects.md](06-compute-effects.md) - 次フェーズ
