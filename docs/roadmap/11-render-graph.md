# Roadmap: Phase 11 - Render Graph（レンダリングエンジンアーキテクチャ）

**プロジェクト**: `D:/dev/shader/render-graph/`
**API**: Direct3D 12
**目標**: ゲームエンジン品質のレンダリングフレームワークを設計・実装する

---

## 習得する概念

| 概念 | 関連ドキュメント |
|---|---|
| GPU 最適化・Async Compute | [16-optimization.md](../concepts/16-optimization.md) |
| Compute Shader・UAV | [15-compute-shaders.md](../concepts/15-compute-shaders.md) |
| D3D12 Resource Barrier | [05-advanced-rendering.md](05-advanced-rendering.md) |

---

## フェーズ分け

### フェーズ 11-1: Render Graph（Frame Graph）の設計

Frostbite エンジン（DICE, GDC 2017）が提案したフレームグラフ。
パスの依存関係をグラフとして宣言し、実行順・バリア・Transient リソースを自動管理する。

**実装項目**:
- `RenderPass` の宣言インターフェース（読み込み / 書き込みリソースを申告）
- DAG（有向非巡回グラフ）の構築
- Topological Sort で実行順序を決定
- 未使用パスのカリング（使われないパスは実行しない）
- リソースのライフタイム計算（最短の生存期間を求める）

```cpp
// Render Graph の使用例（宣言的 API）
class BloomPass : public RenderPass {
    void Setup(RenderGraphBuilder& builder) override {
        // 入力リソースの宣言（SRV として使用）
        m_hdrInput = builder.Read(hdrBufferHandle);

        // 出力リソースの宣言（新規 Transient テクスチャを確保）
        m_bloomOutput = builder.Write(
            builder.CreateTexture("BloomOutput", TextureDesc{width/2, height/2, DXGI_FORMAT_R16G16B16A16_FLOAT})
        );
    }

    void Execute(RenderGraphContext& ctx, ID3D12GraphicsCommandList* cmd) override {
        auto* hdrSRV  = ctx.GetSRV(m_hdrInput);
        auto* bloomRTV = ctx.GetRTV(m_bloomOutput);
        // 実際の描画コマンド
    }
private:
    ResourceHandle m_hdrInput, m_bloomOutput;
};
```

---

### フェーズ 11-2: Transient Resource（一時リソース）管理

**実装項目**:
- パスの実行順を元に Transient テクスチャ / バッファの生存区間を計算
- Aliasing（エイリアシング）: 重ならない生存区間のリソースはメモリを共有
- `ID3D12Heap` + `PlacedResource` でメモリを明示的に制御
- ピークメモリ使用量の最小化（ヒューリスティックパッキング）

```cpp
// Aliasing の例（疑似コード）
// Frame N の G-Buffer はライティング後に不要 → Bloom バッファと同じメモリを使える
//   G-Buffer:  [=====]
//   BloomTemp:         [===]
//   LightingOut: [=========]  ← 共有不可（重複あり）

ID3D12Resource* CreatePlacedResource(ID3D12Heap* heap, UINT64 offset, const D3D12_RESOURCE_DESC& desc) {
    ID3D12Resource* resource = nullptr;
    device->CreatePlacedResource(heap, offset, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                  IID_PPV_ARGS(&resource));
    return resource;
}
```

---

### フェーズ 11-3: 自動 Resource Barrier 挿入

**実装項目**:
- グラフのエッジ（読み書き依存）から必要なバリアを自動決定
- 不要なバリア（同じ State が続く場合）を省略
- Split Barrier（`D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY / END_ONLY`）で GPU ストールを削減
- `ID3D12DebugDevice` でバリア漏れを検出

```cpp
// バリア最適化: Split Barrier の活用
// Begin Barrier（書き込み終了の予告）を早めに発行し、
// End Barrier（読み込み開始）を実際に使う直前に発行

// [Shadow Pass] --BeginBarrier(RTV→SRV)--> ... --EndBarrier--> [Lighting Pass]
// GPU は BeginとEndの間で並列処理できる

commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
    shadowMap, D3D12_RESOURCE_STATE_DEPTH_WRITE,
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
    D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY)); // Begin: ここで移行を予告

// ... 他のパスを実行 ...

commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
    shadowMap, D3D12_RESOURCE_STATE_DEPTH_WRITE,
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
    D3D12_RESOURCE_BARRIER_FLAG_END_ONLY)); // End: ここで実際に使う
```

---

### フェーズ 11-4: Async Compute の統合

**実装項目**:
- Compute キューと Graphics キューの依存関係をグラフで表現
- `ID3D12Fence` でクロスキュー同期
- 並列実行できるパスを自動的に Compute キューに割り当て
- GPU タイムラインの PIX 可視化と最適化

```
フレームタイムライン（理想形）:
  Graphics Q: [ZPrepass] [ShadowPass] [GBuffer] [Lighting] [Post]
  Compute  Q: [LightCull] [SSAO] [Particles] [Mipgen]
                    ↑ Graphics Q の ZPrepass 後から開始
```

---

### フェーズ 11-5: バインドレスレンダリング（Bindless）

**実装項目**:
- 大きなディスクリプタヒープに全テクスチャを登録
- `SM 6.6` の `ResourceDescriptorHeap[]` / `SamplerDescriptorHeap[]` で動的インデックス
- マテリアルバッファにテクスチャインデックスを格納
- ドローコールのステート切り替えを削減

```hlsl
// SM 6.6 Bindless
struct MaterialData {
    uint  albedoIndex;
    uint  normalIndex;
    uint  ormIndex;
    float roughnessScale;
};
StructuredBuffer<MaterialData> materials : register(t0);

float4 BindlessPS(VSOutput input) : SV_Target {
    MaterialData mat = materials[input.materialId];

    // ResourceDescriptorHeap からインデックスでテクスチャを取得
    Texture2D albedo = ResourceDescriptorHeap[mat.albedoIndex];
    Texture2D normal = ResourceDescriptorHeap[mat.normalIndex];
    Texture2D orm    = ResourceDescriptorHeap[mat.ormIndex];

    float3 color = albedo.Sample(SamplerDescriptorHeap[0], input.uv).rgb;
    // ...
    return float4(color, 1.0);
}
```

---

### フェーズ 11-6: マテリアルシステムとシェーダー Permutation

ゲームエンジンのマテリアルシステムの中核。

**実装項目**:
- マテリアル定義（JSON / YAML / カスタムフォーマット）から Permutation を自動生成
- `#define USE_NORMAL_MAP`, `#define USE_PBR` 等のコンパイル時フラグ
- Permutation 爆発の制御（`ShouldCompilePermutation` 相当の除外ロジック）
- シェーダーキャッシュ（バイナリキャッシュ + ハッシュベースの差分更新）

```cpp
// シェーダー Permutation の管理（C++ 疑似コード）
struct ShaderPermutation {
    bool useNormalMap;
    bool usePBR;
    bool useEmissive;
    bool isSkinned;
    int  maxBones; // 0=非スキニング, 64, 256

    // Permutation の総数: 2^4 × 3 = 48
    // ShouldCompile: !isSkinned || maxBones > 0 → 無効な組み合わせを除外
};
```

---

### フェーズ 11-7: シェーダーホットリロード

**実装項目**:
- `ReadDirectoryChangesW`（Win32）または `inotify`（Linux）でシェーダーファイルを監視
- 変更検出 → 非同期 dxc コンパイル
- コンパイル成功後、次フレームから新 PSO に差し替え
- コンパイルエラーは古い PSO を維持（安全なフォールバック）

---

### フェーズ 11-8: Virtual Texturing / Sparse Texture（発展）

**実装項目**:
- `ID3D12Resource` の `RESERVED_RESOURCE`（スパースリソース）
- ページテーブル（Feedback Buffer）を元に必要なタイルだけロード
- ミップマップのストリーミング
- UE5 の Virtual Texture と同等の仕組みの理解

---

### フェーズ 11-9: GPU メモリ管理

**実装項目**:
- `ID3D12Heap` の種類（DEFAULT / UPLOAD / READBACK）
- アップロードリングバッファ（CPU → GPU 転送の効率化）
- フレームパラメータバッファ（CB の毎フレーム更新）
- デファードデリート（使用中リソースの削除タイミング管理）

```cpp
// アップロードリングバッファ（毎フレームの CB 更新）
class UploadRingBuffer {
    void* Map(UINT64 size, UINT64 align, D3D12_GPU_VIRTUAL_ADDRESS& outGPU);
    // 内部: ヘッドポインタをフレームごとに進め、
    //       古いフレームが完了したら末尾をラップアラウンド
};
```

---

### フェーズ 11-10: Multi-Draw Indirect + GPU Driven Rendering

**実装項目**:
- シーン全体のメッシュ情報を GPU バッファに格納
- Compute Shader でフラスタムカリング + LOD 選択 → `DrawIndexedIndirect` 引数を生成
- 1 フレームあたりの GPU ドローコール数を極限まで削減
- `ExecuteIndirect` + `CommandSignature` による複数ドローの一括発行

```cpp
// GPU 駆動描画の全体フロー
// 1. GPU に全メッシュの AABB・LOD 情報を渡す
// 2. CullingCS: フラスタムテスト → DrawIndirectArgs を生成
// 3. ExecuteIndirect: GPU が生成したコマンドをそのまま実行
// CPU は Dispatch(1 CS call) + ExecuteIndirect(1 call) のみ
commandList->Dispatch(ceilDiv(meshCount, 64), 1, 1); // カリング CS
commandList->ExecuteIndirect(commandSignature, maxDraws, argsBuffer, 0, countBuffer, 0);
```

---

## ファイル構成（完成時）

```
render-graph/
├── CMakeLists.txt
├── src/
│   ├── engine/
│   │   ├── RenderGraph.cpp/.h         ← グラフ構築・トポロジカルソート
│   │   ├── RenderGraphPass.h          ← パス基底クラス
│   │   ├── RenderGraphResource.cpp/.h ← リソースハンドル・ライフタイム
│   │   ├── TransientAllocator.cpp/.h  ← Aliasing ヒープ管理
│   │   ├── BarrierManager.cpp/.h      ← 自動バリア挿入
│   │   ├── AsyncComputeScheduler.cpp/.h
│   │   ├── ShaderCache.cpp/.h         ← Permutation + ホットリロード
│   │   ├── UploadRingBuffer.cpp/.h
│   │   └── GPUDrivenRenderer.cpp/.h
│   └── passes/
│       ├── (Phase 5〜10 で作ったすべてのパスをここに統合)
└── shaders/
    ├── (Phase 5〜10 のシェーダー)
    └── gpu_culling_cs.hlsl
```

---

## 確認チェックリスト

- [ ] Render Graph の DAG で未使用パスが自動的にスキップされる
- [ ] Transient リソースの Aliasing でメモリ使用量が削減される（PIX で確認）
- [ ] Split Barrier でシャドウパスとメインパスが並列実行できる
- [ ] Bindless で 10000 種類以上のテクスチャを 1 ドローコールで参照できる
- [ ] シェーダーホットリロードでゲーム実行中に見た目が切り替わる
- [ ] GPU 駆動描画で CPU の Draw Call が 1 桁に削減される

---

## 関連ドキュメント
- [10-vfx-rendering.md](10-vfx-rendering.md) - 前フェーズ
- [12-unreal-integration.md](12-unreal-integration.md) - 次フェーズ
- [../concepts/15-compute-shaders.md](../concepts/15-compute-shaders.md)
- [../concepts/16-optimization.md](../concepts/16-optimization.md)
