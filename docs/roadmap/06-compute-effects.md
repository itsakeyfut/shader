# Roadmap: Phase 6 - Compute Effects

**プロジェクト**: `D:/dev/shader/compute-effects/`
**API**: Direct3D 12
**目標**: Compute Shader を使った GPU 駆動のエフェクトシステムを構築する

---

## 習得する概念

| 概念 | 関連ドキュメント |
|---|---|
| Compute Shader 全般 | [15-compute-shaders.md](../concepts/15-compute-shaders.md) |
| GPU 駆動描画 | [15-compute-shaders.md](../concepts/15-compute-shaders.md) |
| 最適化（Wave Intrinsics） | [16-optimization.md](../concepts/16-optimization.md) |
| プロシージャル生成 | [17-procedural-noise.md](../concepts/17-procedural-noise.md) |

---

## フェーズ分け

### フェーズ 6-1: GPU パーティクルシステム（生成・更新）

**実装項目**:
- パーティクルバッファ（`RWStructuredBuffer<Particle>`）
- Dead List（空きスロット管理、`AppendStructuredBuffer`）
- Alive List（生存パーティクル管理）
- Emit CS（CPU から 1 フレーム分の生成量を指示）
- Update CS（物理・ライフタイム・色変化）

```hlsl
struct Particle {
    float3 position;
    float  lifetime;       // 残りライフタイム
    float3 velocity;
    float  maxLifetime;
    float4 color;          // RGBA（時間変化）
    float  size;
    float3 pad;
};

AppendStructuredBuffer<uint>  deadList     : register(u0);
RWStructuredBuffer<Particle>  particles    : register(u1);
RWStructuredBuffer<uint>      aliveListOut : register(u2);
RWByteAddressBuffer           aliveCounter : register(u3);

[numthreads(256, 1, 1)]
void ParticleUpdateCS(uint id : SV_DispatchThreadID) {
    Particle p = particles[id];
    if (p.lifetime <= 0) return;

    p.lifetime  -= deltaTime;
    p.velocity  += gravity * deltaTime;
    p.position  += p.velocity * deltaTime;

    float t      = 1.0 - p.lifetime / p.maxLifetime;
    p.color.a    = 1.0 - t; // フェードアウト
    p.size       = lerp(startSize, endSize, t);

    particles[id] = p;

    if (p.lifetime <= 0) {
        deadList.Append(id);
    } else {
        uint slot;
        aliveCounter.InterlockedAdd(0, 1, slot);
        aliveListOut[slot] = id;
    }
}
```

---

### フェーズ 6-2: Bitonic Sort（GPU ソート）

ソート済みの Alive List を使い、半透明パーティクルを正しい順序で描画する。

**実装項目**:
- Bitonic Merge Sort の CS 実装（複数パス）
- ソートキー: カメラからの距離²（`dot(toCamera, toCamera)`）
- バックフロントソートによる正しいアルファブレンド

```hlsl
[numthreads(BITONIC_BLOCK_SIZE, 1, 1)]
void BitonicSortCS(uint3 id : SV_DispatchThreadID) {
    uint i = id.x;
    uint j = i ^ step;
    if (j <= i) return;

    float di = sortKeys[i];
    float dj = sortKeys[j];
    bool ascending = ((i & stage) == 0);

    if (ascending ? (di > dj) : (di < dj)) {
        sortKeys[i]    = dj; sortKeys[j]    = di;
        sortIndices[i] ^= sortIndices[j];
        sortIndices[j] ^= sortIndices[i];
        sortIndices[i] ^= sortIndices[j];
    }
}
```

---

### フェーズ 6-3: GPU パーティクルの描画（DrawIndirect）

**実装項目**:
- `DrawIndexedIndirectArgs` バッファを CS が生成
- `ExecuteIndirect` / `DrawIndirect` で CPU 介入なし描画
- Billboard（カメラ向き四角形）VS での生成（`SV_VertexID` を活用）
- テクスチャアトラス・フリップブックアニメーション

```hlsl
// VS: パーティクルを Billboard に展開
VSOutput ParticleVS(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID) {
    uint particleIdx = aliveList[instanceId];
    Particle p       = particles[particleIdx];

    // 四角形の4頂点 (vertexId 0-3) の UV を計算
    float2 localUV = float2(vertexId & 1, (vertexId >> 1) & 1);
    float2 offset  = (localUV - 0.5) * p.size;

    // カメラ空間でオフセット（ビルボード）
    float3 cameraRight = float3(viewMatrix[0][0], viewMatrix[1][0], viewMatrix[2][0]);
    float3 cameraUp    = float3(viewMatrix[0][1], viewMatrix[1][1], viewMatrix[2][1]);
    float3 worldPos    = p.position + cameraRight * offset.x + cameraUp * offset.y;

    output.position = mul(viewProj, float4(worldPos, 1.0));
    output.uv       = localUV;
    output.color    = p.color;
    return output;
}
```

---

### フェーズ 6-4: GPU スキニング

**実装項目**:
- Bone マトリクスバッファ（`StructuredBuffer<float4x4>`）
- Skinning CS（4 ウェイトの線形ブレンド）
- Dual Quaternion スキニング（DQS、関節の体積保存）
- 出力を VS 入力バッファとして再使用

```hlsl
[numthreads(64, 1, 1)]
void SkinningCS(uint id : SV_DispatchThreadID) {
    if (id >= vertexCount) return;
    SkinVertex sv = srcVertices[id];

    // Linear Blend Skinning
    float4x4 skin = boneMatrices[sv.boneIdx.x] * sv.boneWeight.x
                  + boneMatrices[sv.boneIdx.y] * sv.boneWeight.y
                  + boneMatrices[sv.boneIdx.z] * sv.boneWeight.z
                  + boneMatrices[sv.boneIdx.w] * sv.boneWeight.w;

    dstVertices[id].position = mul(skin, float4(sv.position, 1.0)).xyz;
    dstVertices[id].normal   = normalize(mul((float3x3)skin, sv.normal));
    dstVertices[id].tangent  = float4(normalize(mul((float3x3)skin, sv.tangent.xyz)), sv.tangent.w);
}
```

---

### フェーズ 6-5: モーフターゲット（Blend Shapes）

フェイシャルアニメーション・形状変形のための GPU モーフィング。

**実装項目**:
- デルタバッファ（ベースメッシュとの差分頂点データ）
- モーフ重みバッファ
- Morph CS（スキニング後の位置・法線にデルタを加算）

```hlsl
[numthreads(64, 1, 1)]
void MorphCS(uint id : SV_DispatchThreadID) {
    float3 deltaPos = float3(0, 0, 0);
    float3 deltaNrm = float3(0, 0, 0);

    for (uint i = 0; i < numMorphTargets; i++) {
        MorphDelta d = morphDeltas[i * maxVertices + id];
        deltaPos += d.position * morphWeights[i];
        deltaNrm += d.normal   * morphWeights[i];
    }
    outVertices[id].position += deltaPos;
    outVertices[id].normal    = normalize(outVertices[id].normal + deltaNrm);
}
```

---

### フェーズ 6-6: GPU クロスシミュレーション（基礎）

**実装項目**:
- 頂点バッファをシミュレーション用に拡張（現在・前フレーム位置）
- Position Based Dynamics（PBD）の Compute Shader 実装
- 距離制約・曲げ制約の反復解決（Jacobi 法）
- コリジョン（簡易球・カプセル）

```hlsl
[numthreads(64, 1, 1)]
void ClothSimCS(uint id : SV_DispatchThreadID) {
    float3 pos     = clothPos[id];
    float3 prevPos = clothPrevPos[id];

    // Verlet 積分
    float3 velocity   = pos - prevPos;
    float3 nextPos    = pos + velocity * (1.0 - damping)
                        + gravity * deltaTime * deltaTime;

    // 固定頂点は動かない
    if (clothFixed[id]) nextPos = clothRestPos[id];

    clothPrevPos[id] = pos;
    clothPos[id]     = nextPos;
}
// 別パスで距離制約を反復
```

---

### フェーズ 6-7: ミップマップ生成 CS

**実装項目**:
- 動的テクスチャのミップ生成（`GenerateMips` の代替）
- ダウンサンプリング CS（4 サンプルの平均）
- sRGB 補正あり / なしの分岐

```hlsl
[numthreads(8, 8, 1)]
void GenerateMipCS(uint3 dtID : SV_DispatchThreadID) {
    if (any(dtID.xy >= mipSize)) return;

    // 上位 Mip から 2×2 を読んで平均
    float2 uv = (float2(dtID.xy) + 0.5) / float2(mipSize);
    float4 c  = srcMip.SampleLevel(linearSamp, uv, 0);
    dstMip[dtID.xy] = c;
}
```

---

### フェーズ 6-8: テクスチャ圧縮 CS（実装調査）

**実装項目**:
- BC1 / BC3 / BC4 / BC5 の GPU エンコード（CS 実装）
- リアルタイムで生成したテクスチャの即時圧縮
- 参考: `DirectXTex` の BC 圧縮コード

---

### フェーズ 6-9: Async Compute の活用

**実装項目**:
- グラフィクスキューと Compute キューの並列実行
- フラスタムカリング CS をグラフィクスパスと並走させる
- `ID3D12Fence` で Graphics-Compute 同期
- `D3D12_COMMAND_LIST_TYPE_COMPUTE` キューの作成

```
タイムライン例:
  Graphics Queue: [G-Buffer Pass] → [Lighting Pass] → [Post]
  Compute Queue:  [Light Culling] → [Particle Update]
                   ↑ Shadow Pass 後に開始可能（Fence で同期）
```

---

## ファイル構成（完成時）

```
compute-effects/
├── CMakeLists.txt
├── src/
│   ├── D3D12App.cpp/.h
│   ├── ParticleSystem.cpp/.h    ← Emit/Update/Sort/Draw
│   ├── GpuSkinning.cpp/.h
│   ├── MorphTargets.cpp/.h
│   ├── ClothSimulation.cpp/.h
│   └── AsyncComputeQueue.cpp/.h
└── shaders/
    ├── particle_emit_cs.hlsl
    ├── particle_update_cs.hlsl
    ├── particle_sort_cs.hlsl    ← Bitonic Sort
    ├── particle_vs.hlsl / particle_ps.hlsl
    ├── skinning_cs.hlsl
    ├── morph_cs.hlsl
    ├── cloth_sim_cs.hlsl
    └── mipgen_cs.hlsl
```

---

## 確認チェックリスト

- [ ] 10 万パーティクルが 60fps で更新・描画できる
- [ ] Bitonic Sort の安定性をランダムデータで検証した
- [ ] DrawIndirect で CPU-GPU の往復がないことを PIX で確認した
- [ ] GPU スキニングの結果が CPU スキニングと一致する
- [ ] Async Compute のオーバーラップを PIX タイムラインで確認した
- [ ] Wave Intrinsics で Prefix Sum が正しく動作する

---

## 関連ドキュメント
- [05-advanced-rendering.md](05-advanced-rendering.md) - 前フェーズ
- [../concepts/15-compute-shaders.md](../concepts/15-compute-shaders.md)
- [07-ray-tracing.md](07-ray-tracing.md) - 次フェーズ
