# 15 - Compute Shaders（計算シェーダー）

## スレッドグループと DispatchThreadID

```hlsl
// numthreads(X, Y, Z): グループ内のスレッド数
// X × Y × Z ≤ 1024（D3D11/12の上限）
// X と Y は 1 〜 1024、Z は 1 〜 64

[numthreads(8, 8, 1)]
void CSMain(
    uint3 dtID : SV_DispatchThreadID,  // グローバルスレッドID (groupID * groupSize + localID)
    uint3 gID  : SV_GroupID,           // グループID
    uint3 gtID : SV_GroupThreadID,     // グループ内ローカルID
    uint  gi   : SV_GroupIndex         // グループ内フラットインデックス (gtID.z*Xsize*Ysize + gtID.y*Xsize + gtID.x)
) {
    // 1920×1080 テクスチャ → Dispatch(ceil(1920/8), ceil(1080/8), 1) = Dispatch(240, 135, 1)
    if (dtID.x >= width || dtID.y >= height) return; // 境界チェック
}
```

### Dispatch の計算
```cpp
UINT groupX = (width  + 7) / 8;  // 8スレッドグループ、切り上げ
UINT groupY = (height + 7) / 8;
context->Dispatch(groupX, groupY, 1);
```

---

## GroupShared Memory

同一グループ内スレッドが共有できる高速メモリ（L1 キャッシュ相当）。

```hlsl
#define TILE_SIZE 8
#define TILE_PIXELS (TILE_SIZE * TILE_SIZE)

groupshared float4 sharedColors[TILE_PIXELS]; // グループ内共有

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void TiledBlurCS(uint3 dtID : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    // 各スレッドが1ピクセルを共有メモリにロード
    sharedColors[gi] = inputTex[dtID.xy];

    // 全スレッドがロードを完了するまで待機（必須！）
    GroupMemoryBarrierWithGroupSync();

    // 共有メモリから近傍ピクセルにアクセス（高速）
    float4 sum = float4(0, 0, 0, 0);
    // ... ローカルインデックスで近傍にアクセス

    outputTex[dtID.xy] = sum / KERNEL_SIZE;
}
```

### メモリ制限
- 上限: **32KB**（D3D11、カード依存で実際はそれ以下のことも）
- `float4[64]` = 4byte × 4 × 64 = 1024 bytes（余裕あり）
- `float4[1024]` = 16KB（ギリギリ）

---

## GroupMemoryBarrierWithGroupSync

```hlsl
// GroupMemoryBarrierWithGroupSync():
//   - すべてのスレッドがこの行に到達するまでブロック
//   - 共有メモリへの書き込みが全スレッドに見える状態にする

// DeviceMemoryBarrier():
//   - グローバルメモリ（UAV）の同期

// AllMemoryBarrierWithGroupSync():
//   - 共有メモリ + グローバルメモリ の両方を同期
```

---

## UAV（Unordered Access View）

Compute Shader で読み書きできるリソース。

```hlsl
// 書き込み可能テクスチャ
RWTexture2D<float4>   outputTex      : register(u0);
// 読み書き可能バッファ
RWStructuredBuffer<Particle> particles : register(u1);
// アトミック操作対応バッファ
RWByteAddressBuffer  atomicBuf       : register(u2);

// 使用例
outputTex[uint2(x, y)]   = float4(color, 1.0);
particles[id].position   += particles[id].velocity * deltaTime;

// アトミック加算（ヒストグラム構築などに使用）
uint prev;
atomicBuf.InterlockedAdd(bucketOffset, 1, prev);
```

---

## GPU パーティクル（更新・ソート・描画）

### パーティクル更新
```hlsl
struct Particle {
    float3 position;
    float3 velocity;
    float  lifetime;
    float  maxLifetime;
};

[numthreads(256, 1, 1)]
void ParticleUpdateCS(uint id : SV_DispatchThreadID) {
    if (id >= MAX_PARTICLES) return;

    Particle p = particles[id];

    // ライフタイム更新
    p.lifetime -= deltaTime;
    if (p.lifetime <= 0) {
        // 死亡 → Dead List に追加（アトミック）
        uint slot;
        deadListCounter.InterlockedAdd(0, 1, slot);
        deadList[slot] = id;
        p.lifetime = 0;
        particles[id] = p;
        return;
    }

    // 物理シミュレーション
    p.velocity += gravity * deltaTime;
    p.position += p.velocity * deltaTime;

    particles[id] = p;
}
```

### GPU ソート（Bitonic Sort）
```hlsl
// Bitonic Merge Sort の1パス
[numthreads(BLOCK_SIZE, 1, 1)]
void BitonicSortCS(uint3 id : SV_DispatchThreadID) {
    uint i = id.x;
    uint j = id.x ^ step; // 比較ペアのインデックス

    if (j > i) {
        // 値を比較してスワップ
        float di = keys[i], dj = keys[j];
        bool ascending = (id.x & stage) == 0;
        if ((ascending && di > dj) || (!ascending && di < dj)) {
            keys[i] = dj; keys[j] = di;
        }
    }
}
```

---

## GPU スキニング

CPU でのスキニングをGPUに移行。

```hlsl
struct Vertex {
    float3 position;
    float3 normal;
    uint4  boneIndices; // 影響を受ける骨のインデックス
    float4 boneWeights; // 各骨の影響度
};

cbuffer BoneData : register(b0) {
    float4x4 boneMatrices[MAX_BONES];
};

[numthreads(64, 1, 1)]
void SkinningCS(uint id : SV_DispatchThreadID) {
    if (id >= vertexCount) return;

    Vertex v = inputVertices[id];

    // 4つの骨行列を重み付き合成
    float4x4 skinMatrix = boneMatrices[v.boneIndices.x] * v.boneWeights.x
                        + boneMatrices[v.boneIndices.y] * v.boneWeights.y
                        + boneMatrices[v.boneIndices.z] * v.boneWeights.z
                        + boneMatrices[v.boneIndices.w] * v.boneWeights.w;

    outputVertices[id].position = mul(skinMatrix, float4(v.position, 1.0)).xyz;
    outputVertices[id].normal   = normalize(mul((float3x3)skinMatrix, v.normal));
}
```

---

## タイルベースライトカリング（Forward+ 実装）

```hlsl
// グループ共有メモリで光源リストを構築
groupshared uint tileMinDepth;
groupshared uint tileMaxDepth;
groupshared uint tileLightCount;
groupshared uint tileLightIndices[MAX_LIGHTS_PER_TILE];

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void LightCullingCS(
    uint3 groupID   : SV_GroupID,
    uint3 threadID  : SV_GroupThreadID,
    uint3 dispatchID : SV_DispatchThreadID,
    uint  gi        : SV_GroupIndex
) {
    // 初期化（1スレッドのみ）
    if (gi == 0) {
        tileMinDepth = 0x7F7FFFFF;
        tileMaxDepth = 0;
        tileLightCount = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    // タイルの深度範囲をアトミックで計算
    float depth = depthBuffer[dispatchID.xy].r;
    uint  depthInt = asuint(depth);
    InterlockedMin(tileMinDepth, depthInt);
    InterlockedMax(tileMaxDepth, depthInt);
    GroupMemoryBarrierWithGroupSync();

    float minZ = asfloat(tileMinDepth);
    float maxZ = asfloat(tileMaxDepth);

    // タイルの視錐台（Frustum）を計算
    float4 frustumPlanes[4] = CalcTileFrustum(groupID.xy, minZ, maxZ);

    // 各スレッドが一部のライトを担当してカリング
    uint lightStride = TILE_SIZE * TILE_SIZE; // グループのスレッド数
    for (uint i = gi; i < numLights; i += lightStride) {
        if (LightInsideFrustum(lights[i], frustumPlanes)) {
            uint idx;
            InterlockedAdd(tileLightCount, 1, idx);
            if (idx < MAX_LIGHTS_PER_TILE)
                tileLightIndices[idx] = i;
        }
    }
    GroupMemoryBarrierWithGroupSync();

    // 結果をグローバルバッファに書き出し
    if (gi == 0) {
        uint tileIdx = groupID.y * numTilesX + groupID.x;
        lightGrid[tileIdx * 2 + 0] = min(tileLightCount, MAX_LIGHTS_PER_TILE);
        lightGrid[tileIdx * 2 + 1] = tileIdx * MAX_LIGHTS_PER_TILE;
    }
    // インデックスリストを書き出し...
}
```

---

## GPU 駆動描画（DrawIndirect / DispatchIndirect）

CPU-GPU 往復なしにGPUがドローコールを生成する。

```hlsl
// Culling CS がドローコールを生成
struct DrawIndexedIndirectArgs {
    uint IndexCountPerInstance;
    uint InstanceCount;
    uint StartIndexLocation;
    int  BaseVertexLocation;
    uint StartInstanceLocation;
};

[numthreads(64, 1, 1)]
void FrustumCullingCS(uint id : SV_DispatchThreadID) {
    if (id >= objectCount) return;

    BoundingBox aabb = objectAABBs[id];
    if (FrustumCull(aabb, frustumPlanes)) {
        return; // カリング
    }

    // ドローコールをインダイレクトバッファに追加
    uint slot;
    InterlockedAdd(drawCount[0], 1, slot);

    DrawIndexedIndirectArgs args;
    args.IndexCountPerInstance = objects[id].indexCount;
    args.InstanceCount         = 1;
    args.StartIndexLocation    = objects[id].startIndex;
    args.BaseVertexLocation    = objects[id].baseVertex;
    args.StartInstanceLocation = slot; // インスタンスIDとして使用

    drawArgs[slot] = args;
}
```

```cpp
// CPU 側
context->ExecuteIndirect(drawCommandSignature, maxDraws,
    drawArgsBuffer, 0, drawCountBuffer, 0);
```

---

## 関連ドキュメント
- [14-advanced-rendering.md](14-advanced-rendering.md) - Forward+ タイルカリング
- [09-ibl.md](09-ibl.md) - IBL 事前計算（Compute Shader）
- [17-procedural-noise.md](17-procedural-noise.md) - ノイズ生成（CS）
