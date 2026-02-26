# 16 - Optimization（最適化）

## ALU バウンド vs Memory バウンド

シェーダーのボトルネックを特定することが最初のステップ。

### ALU バウンド（演算バウンド）
- GPU の計算ユニット（SIMD/ALU）が処理しきれない
- 症状: 命令数が多いシェーダー、transcendental 関数（sin/cos/sqrt）の多用
- 対策:
  - 計算を事前計算して定数バッファに渡す
  - 安価な近似（`rsqrt` ≒ `1/sqrt`、多項式近似）
  - MAD 命令の活用（`a*b+c` は1命令）

### Memory バウンド（帯域幅バウンド）
- テクスチャサンプリング・バッファ読み込みがボトルネック
- 症状: テクスチャキャッシュミスが多い、大きなバッファの読み込み
- 対策:
  - テクスチャの圧縮（BC1〜BC7、ASTC）
  - UV アクセスパターンの改善（キャッシュフレンドリー）
  - バッファのパッキング（Structure of Arrays vs Array of Structures）
  - GroupShared メモリの活用

### プロファイリングツール
- **RenderDoc**: フレームキャプチャ、シェーダーデバッグ
- **PIX for Windows**: GPU キャプチャ、タイムライン
- **NVIDIA Nsight Graphics**: GPU パフォーマンスカウンター
- **AMD Radeon GPU Profiler**: AMDカード向け詳細分析

---

## Z-Prepass / Early-Z / Hi-Z Occlusion

### Z-Prepass
最初のパスで深度のみ書き込み、次のパスで Pixel Shader を省略。

```cpp
// Pass 1: 深度のみ（PS なし）
context->PSSetShader(nullptr, nullptr, 0);
context->OMSetRenderTargets(0, nullptr, dsv);
context->Draw(vertexCount, 0);

// Pass 2: 深度テスト = EQUAL（深度変更なし）
D3D11_DEPTH_STENCIL_DESC dsDesc = {};
dsDesc.DepthEnable    = TRUE;
dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO; // 書き込みなし
dsDesc.DepthFunc      = D3D11_COMPARISON_EQUAL;       // 等しい場合のみ通過
```

**効果**: Overdraw が多いシーンで PS の実行数を削減。

### Early-Z（ハードウェア自動）
シェーダー実行前に深度テストを行いフラグメントを早期破棄。
`SV_Depth` の上書きや `clip()` があると無効化される。

```hlsl
// Early-Z を無効化してしまう例
[earlydepthstencil] // これで明示的に有効化（clipとの共存策）
float4 PSMain() : SV_Target {
    // clip() があっても earlydepthstencil アトリビュートで Early-Z を強制
}
```

### Hi-Z（Hierarchical-Z）オクルージョン
GPU が深度バッファのミップマップを使い、オクルージョンをブロック単位で高速テスト。
ドライバが自動的に行う（GPU によって異なる）。

---

## GPU インスタンシング

同じメッシュを1回のドローコールで多数描画。

```hlsl
// インスタンスごとのデータ
StructuredBuffer<float4x4> instanceMatrices : register(t10);

VSOutput VSMain(VSInput input, uint instanceID : SV_InstanceID) {
    float4x4 world = instanceMatrices[instanceID];
    output.position = mul(mul(viewProj, world), float4(input.pos, 1.0));
    // ...
}
```

```cpp
// DrawIndexedInstanced: 1回のドローコールで N インスタンス
context->DrawIndexedInstanced(indexCount, instanceCount, 0, 0, 0);
```

**適用例**: 草・木・岩・パーティクル（同一メッシュの大量描画）

---

## Frustum Culling / Occlusion Culling

### CPU Frustum Culling
```cpp
bool FrustumCull(const XMFLOAT3& center, const XMFLOAT3& extents,
                 const XMFLOAT4 planes[6]) {
    for (int i = 0; i < 6; i++) {
        float d = XMVectorGetX(XMVector3Dot(
            XMLoadFloat3(&center), XMLoadFloat4(&planes[i]))) + planes[i].w;
        float r = abs(extents.x * planes[i].x)
                + abs(extents.y * planes[i].y)
                + abs(extents.z * planes[i].z);
        if (d + r < 0) return false; // 完全に視錐台外
    }
    return true;
}
```

### GPU Occlusion Culling（Z-Pyramid）
- 深度バッファのミップマップ（Z-Pyramid）を生成
- Compute Shader でオブジェクト AABB と Z-Pyramid を比較
- オクルードされたオブジェクトは DrawIndirect のカウントに含めない

---

## LOD（Level of Detail）

距離に応じてメッシュ品質を切り替える。

```hlsl
// LOD 選択（VS で距離計算）
float dist = distance(worldPos, cameraPos);
uint  lod  = dist < 10.0 ? 0 :
             dist < 30.0 ? 1 :
             dist < 100.0 ? 2 : 3;

// Mesh LOD ファイルを別バッファとして用意
// または GPU でのテッセレーション係数調整
```

**Screen Space Error 方式**: 投影後のピクセルサイズでLOD選択（より品質が安定）

---

## Mesh Shaders（DirectX 12 Ultimate）

Amplification Shader + Mesh Shader がジオメトリパイプラインを置き換え。
Vertex Buffer / Input Layout が不要になる。

```hlsl
// Mesh Shader: 最大 256 頂点・512 プリミティブを出力
[numthreads(128, 1, 1)]
[outputtopology("triangle")]
void MeshMain(
    uint gtid : SV_GroupThreadID,
    out vertices VSOutput verts[MAX_VERTS],
    out indices  uint3    tris[MAX_TRIS]
) {
    SetMeshOutputCounts(actualVerts, actualTris);

    // メッシュレット（Meshlet）単位で頂点・インデックスを生成
    if (gtid < actualVerts) {
        verts[gtid].position = FetchVertex(gtid);
    }
    if (gtid < actualTris) {
        tris[gtid] = FetchTriangle(gtid);
    }
}

// Amplification Shader: Culling してから Mesh Shader を起動
[numthreads(32, 1, 1)]
void AmplificationMain(uint dtid : SV_DispatchThreadID) {
    bool visible = FrustumCull(meshlets[dtid].bounds);
    if (visible) {
        DispatchMesh(1, 1, 1); // Mesh Shader を起動
    }
}
```

---

## Variable Rate Shading（VRS）

画面の領域ごとにシェーダー実行レートを変化させる（DirectX 12 Ultimate）。

```
フル解像度 (1x1): エッジ・中央の重要部分
2x1 or 1x2:      水平/垂直方向に2ピクセルに1回のみPS実行
2x2:             4ピクセルに1回（外周・単純な背景）
4x4:             16ピクセルに1回（完全なスカイボックス等）
```

```cpp
// D3D12 VRS 設定
D3D12_SHADING_RATE_COMBINER combiners[] = {
    D3D12_SHADING_RATE_COMBINER_MAX, // Primitive Rate との合成
    D3D12_SHADING_RATE_COMBINER_MAX  // Screen Space Image との合成
};
commandList->RSSetShadingRate(D3D12_SHADING_RATE_2X2, combiners);
```

---

## Wave Intrinsics（Shader Model 6.0+）

SIMD グループ（Wave = Warp = 32〜64 スレッド）内での高速データ共有。

```hlsl
// Wave 内のすべてのスレッドが条件を満たすか
bool allTrue = WaveActiveAllTrue(value > 0);

// Wave 内の値を合計（GroupShared 不要）
float sum = WaveActiveSum(myValue);

// Wave 内の最小値
float minVal = WaveActiveMin(myDepth);

// Prefix Sum（パーティクルアロケーション等で有用）
uint prefixCount = WavePrefixCountBits(isAlive);

// Lane インデックス（0〜WaveSize-1）
uint laneIdx = WaveGetLaneIndex();
uint waveSize = WaveGetLaneCount(); // 通常 32 or 64
```

---

## 最適化チェックリスト

### シェーダー
- [ ] `[branch]` を使い、不要なシェーダー実行を避ける
- [ ] Texture2D Array でバインド数を削減
- [ ] `half` 型をモバイル向けに活用
- [ ] MAD 命令になるよう計算順序を工夫

### Draw Call
- [ ] インスタンシングで同一メッシュをまとめる
- [ ] Frustum Culling で見えないオブジェクトをスキップ
- [ ] Z-Prepass で Overdraw を削減
- [ ] DrawIndirect で CPU-GPU 往復を削減

### テクスチャ
- [ ] BC7（高品質）/ BC1（カラー）/ BC4（グレー）で圧縮
- [ ] 適切なミップマップを生成
- [ ] テクスチャアトラスで SRV バインド数削減

---

## 関連ドキュメント
- [15-compute-shaders.md](15-compute-shaders.md) - GPU 駆動描画
- [00-graphics-pipeline.md](00-graphics-pipeline.md) - パイプラインの基礎
