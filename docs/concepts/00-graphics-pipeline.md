# 00 - Graphics Pipeline

## GPU パイプライン全体図

```
Input Assembler (IA)
        ↓
Vertex Shader (VS)          ← プログラマブル
        ↓
Hull Shader (HS)            ← プログラマブル（テッセレーション制御）
        ↓
Tessellator (DS-Fixed)      ← Fixed-function
        ↓
Domain Shader (DS)          ← プログラマブル（テッセレーション後頂点）
        ↓
Geometry Shader (GS)        ← プログラマブル（省略可能）
        ↓
Rasterizer (RS)             ← Fixed-function
        ↓
Pixel Shader (PS)           ← プログラマブル
        ↓
Output Merger (OM)          ← Fixed-function（ブレンド・深度ステンシル）
```

---

## 各ステージの役割と入出力

### Input Assembler (IA)
- **役割**: 頂点バッファ・インデックスバッファからプリミティブを組み立てる
- **入力**: `ID3D11Buffer`（頂点/インデックス）、Input Layout
- **出力**: 頂点ストリーム（位置・色・UV など）
- **設定**: `IASetVertexBuffers`, `IASetIndexBuffer`, `IASetPrimitiveTopology`

### Vertex Shader (VS)
- **役割**: 各頂点を変換する（Object→Clip 空間）
- **入力**: IA からの頂点データ（セマンティクスで参照）
- **出力**: `SV_Position`（必須）＋任意の補間データ
- **処理**: MVP 行列適用、ライティング計算（Gouraud）、UV 変換

### Hull Shader (HS) ※省略可能
- **役割**: パッチ（制御点の集合）のテッセレーション係数を決定
- **入力**: パッチ（三角形・クワッド・ライン）
- **出力**: テッセレーション係数（`SV_TessFactor`）

### Tessellator (Fixed-function)
- 指定されたテッセレーション係数に従いプリミティブを細分化

### Domain Shader (DS)
- **役割**: 細分化後の各頂点の最終位置を計算
- **入力**: 重心座標（u, v, w）、制御点
- **出力**: `SV_Position`

### Geometry Shader (GS) ※省略可能
- **役割**: プリミティブ単位で頂点を生成・削除・変換
- **用途**: シャドウボリューム、パーティクル展開、Cube Map レンダリング
- **注意**: 現代 GPU では遅い場合が多い。Compute Shader で代替を検討

### Rasterizer (RS)
- **役割**: ベクター→ピクセルへの離散化
- **処理**:
  - クリッピング（Clip 空間）
  - 透視除算（→ NDC）
  - ビューポート変換（→ Screen 空間）
  - バックフェースカリング
  - 深度値の補間
  - 属性の透視補正補間
- **設定**: `RSSetViewports`, `RSSetState`（`D3D11_RASTERIZER_DESC`）

### Pixel Shader (PS)
- **役割**: 各フラグメントの色を計算
- **入力**: ラスタライザが補間した頂点データ
- **出力**: `SV_Target`（色）、オプションで `SV_Depth`
- **処理**: テクスチャサンプリング、ライティング、シャドウ

### Output Merger (OM)
- **役割**: PS 出力と既存バッファのブレンド・深度テスト
- **処理**:
  - 深度テスト（`D3D11_COMPARISON_LESS` など）
  - ステンシルテスト
  - アルファブレンディング（`D3D11_BLEND_DESC`）
- **出力**: RTV（Render Target View）、DSV（Depth Stencil View）

---

## Fixed-function vs Programmable ステージ

| ステージ | 種別 | 変更可能な設定 |
|---|---|---|
| Input Assembler | Fixed-function | Input Layout、プリミティブトポロジー |
| Vertex Shader | **Programmable** | 任意の HLSL コード |
| Rasterizer | Fixed-function | Viewport、カリングモード、Fill Mode |
| Pixel Shader | **Programmable** | 任意の HLSL コード |
| Output Merger | Fixed-function | Blend State、Depth Stencil State |

---

## Draw Call と状態管理

Draw Call 前に必要な状態設定（D3D11 例）:

```cpp
// IA
context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
context->IASetIndexBuffer(indexBuffer, DXGI_FORMAT_R32_UINT, 0);
context->IASetInputLayout(inputLayout);
context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

// VS
context->VSSetShader(vertexShader, nullptr, 0);
context->VSSetConstantBuffers(0, 1, &cbuffer);

// RS
context->RSSetViewports(1, &viewport);
context->RSSetState(rasterizerState);

// PS
context->PSSetShader(pixelShader, nullptr, 0);
context->PSSetShaderResources(0, 1, &srv);
context->PSSetSamplers(0, 1, &sampler);

// OM
context->OMSetRenderTargets(1, &rtv, dsv);
context->OMSetDepthStencilState(depthState, 0);

// Draw
context->DrawIndexed(indexCount, 0, 0);
```

**状態管理の注意点**:
- D3D11 は即時モード。状態は次の変更まで維持される（ステートリーク注意）
- D3D12/Vulkan はコマンドリスト。状態はコマンドバッファ内でのみ有効

---

## D3D11 / D3D12 / Vulkan の対応関係

| 概念 | D3D11 | D3D12 | Vulkan |
|---|---|---|---|
| コンテキスト | `ID3D11DeviceContext` | `ID3D12GraphicsCommandList` | `VkCommandBuffer` |
| パイプライン状態 | 個別ステート（Blend/RS/DS） | `ID3D12PipelineState` | `VkPipeline` |
| リソースバインド | SRV/UAV/CBV/Sampler | Root Signature + Descriptor Heap | Descriptor Set + Layout |
| 同期 | 暗黙的（ドライバが管理） | Fence + Resource Barrier | Semaphore + Memory Barrier |
| レンダーパス | 不要（暗黙的） | 不要（バリアで管理） | `VkRenderPass`（明示的） |
| バックバッファ | `IDXGISwapChain` | `IDXGISwapChain3` | `VkSwapchainKHR` |

### 学習の進め方
- **Phase 1-2**: D3D11（シンプルな API、概念学習に集中）
- **Phase 3+**: D3D12（明示的な制御、GPU 駆動描画、Ray Tracing）
- Vulkan は D3D12 の概念が身についた後に並行学習

---

## 関連ドキュメント
- [02-coordinate-spaces.md](02-coordinate-spaces.md) - 空間変換の詳細
- [03-vertex-transformation.md](03-vertex-transformation.md) - VS での行列計算
- [01-hlsl-fundamentals.md](01-hlsl-fundamentals.md) - HLSL の書き方
