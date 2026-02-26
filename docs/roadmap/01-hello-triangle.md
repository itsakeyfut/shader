# Roadmap: Phase 1 - Hello Triangle

**プロジェクト**: `D:/dev/shader/hello-triangle/`
**API**: Direct3D 11
**目標**: GPU パイプラインを1度通す / HLSL の最初のシェーダーを動かす

---

## 習得する概念

| 概念 | 関連ドキュメント |
|---|---|
| GPU パイプライン全体 | [00-graphics-pipeline.md](../concepts/00-graphics-pipeline.md) |
| HLSL の基礎構文 | [01-hlsl-fundamentals.md](../concepts/01-hlsl-fundamentals.md) |
| 座標空間 | [02-coordinate-spaces.md](../concepts/02-coordinate-spaces.md) |
| MVP 変換 | [03-vertex-transformation.md](../concepts/03-vertex-transformation.md) |
| テクスチャサンプリング | [05-texturing-sampling.md](../concepts/05-texturing-sampling.md) |

---

## フェーズ分け

### フェーズ 1-1: D3D11 初期化

**実装項目**:
- `IDXGISwapChain` / `IDXGISwapChain1` の作成
- `ID3D11Device` + `ID3D11DeviceContext` の作成
- バックバッファから `ID3D11RenderTargetView` を取得
- `D3D11_VIEWPORT` の設定
- メッセージループ

**確認方法**: ウィンドウが開き、単色でクリアされる

```cpp
// 最小限のD3D11初期化
DXGI_SWAP_CHAIN_DESC scd = {};
scd.BufferCount          = 2;
scd.BufferDesc.Format    = DXGI_FORMAT_R8G8B8A8_UNORM;
scd.BufferUsage          = DXGI_USAGE_RENDER_TARGET_OUTPUT;
scd.OutputWindow         = hwnd;
scd.SampleDesc.Count     = 1;
scd.Windowed             = TRUE;
scd.SwapEffect           = DXGI_SWAP_EFFECT_FLIP_DISCARD;

D3D11CreateDeviceAndSwapChain(
    nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
    D3D11_CREATE_DEVICE_DEBUG,          // デバッグレイヤー有効
    nullptr, 0, D3D11_SDK_VERSION,
    &scd, &swapChain, &device, nullptr, &context
);
```

---

### フェーズ 1-2: 頂点バッファ・Input Layout・Draw Call

**実装項目**:
- 頂点構造体の定義（位置・色）
- `ID3D11Buffer`（頂点バッファ）の作成
- `D3D11_INPUT_ELEMENT_DESC` と `ID3D11InputLayout` の作成
- `IASetPrimitiveTopology(TRIANGLE_LIST)`
- `Draw(3, 0)` で三角形を描画

```cpp
struct Vertex {
    float pos[3];
    float col[4];
};

Vertex verts[] = {
    { { 0.0f,  0.5f, 0.0f}, {1,0,0,1} },
    { { 0.5f, -0.5f, 0.0f}, {0,1,0,1} },
    { {-0.5f, -0.5f, 0.0f}, {0,0,1,1} },
};
```

---

### フェーズ 1-3: 最小限の VS / PS（単色三角形）

**VS（Vertex Shader）**:
```hlsl
struct VSInput {
    float3 position : POSITION;
    float4 color    : COLOR0;
};

struct VSOutput {
    float4 position : SV_Position;
    float4 color    : COLOR0;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    output.position = float4(input.position, 1.0); // NDC 座標をそのまま使用
    output.color    = input.color;
    return output;
}
```

**PS（Pixel Shader）**:
```hlsl
float4 PSMain(VSOutput input) : SV_Target {
    return input.color;
}
```

**確認方法**: RGB の三角形が画面中央に表示される

---

### フェーズ 1-4: 定数バッファ（MVP 行列・カラー）

**実装項目**:
- `cbuffer` の HLSL 定義
- `D3D11_USAGE_DYNAMIC` バッファの作成
- `Map / Unmap` でデータ更新
- MVP 行列の計算（DirectXMath 使用）

```hlsl
cbuffer PerObject : register(b0) {
    float4x4 mvpMatrix;
    float4   tintColor;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    output.position = mul(mvpMatrix, float4(input.position, 1.0));
    output.color    = input.color * tintColor;
    return output;
}
```

```cpp
// CPU 側: MVP の計算
XMMATRIX model = XMMatrixRotationY(angle);
XMMATRIX view  = XMMatrixLookAtLH(eye, target, up);
XMMATRIX proj  = XMMatrixPerspectiveFovLH(XM_PIDIV4, aspect, 0.1f, 100.f);
XMMATRIX mvp   = XMMatrixTranspose(model * view * proj); // Row-Major → Column-Major
```

**確認方法**: 三角形が画面奥に見え、回転する

---

### フェーズ 1-5: テクスチャマッピング（SRV・Sampler）

**実装項目**:
- 画像ロード（WIC または stb_image）
- `ID3D11Texture2D` + `ID3D11ShaderResourceView` の作成
- `ID3D11SamplerState` の作成
- UV 座標の頂点データへの追加
- `PSSetShaderResources` / `PSSetSamplers` でバインド

```hlsl
Texture2D    albedoTex : register(t0);
SamplerState linearSamp : register(s0);

float4 PSMain(VSOutput input) : SV_Target {
    return albedoTex.Sample(linearSamp, input.uv);
}
```

**確認方法**: 三角形（またはクワッド）にテクスチャが貼られる

---

### フェーズ 1-6: UV アニメーション（タイム CB）

**実装項目**:
- `time` を cbuffer に追加
- VS または PS で `time` を使って UV オフセット
- フレームタイムの計測（`QueryPerformanceCounter`）

```hlsl
cbuffer PerFrame : register(b1) {
    float time;
    float deltaTime;
    float2 padding;
};

float4 PSMain(VSOutput input) : SV_Target {
    float2 animUV = input.uv + float2(time * 0.1, 0.0);
    return albedoTex.Sample(linearSamp, animUV);
}
```

---

### フェーズ 1-7（発展）: D3D12 版へのリファクタリング

**移行のポイント**:
- コマンドリスト・コマンドキューの理解
- リソースバリア（`D3D12_RESOURCE_BARRIER`）
- ディスクリプタヒープ（CBV/SRV/UAV, RTV, DSV）
- Root Signature
- PSO（Pipeline State Object）

**参考**: `DirectX-Graphics-Samples/D3D12HelloWorld`

---

## ファイル構成（完成時）

```
hello-triangle/
├── CMakeLists.txt
├── src/
│   ├── main.cpp           ← ウィンドウ作成・メッセージループ
│   ├── D3DApp.cpp/.h      ← D3D11 初期化・リソース管理
│   ├── Mesh.cpp/.h        ← 頂点バッファ・インデックスバッファ
│   └── Shader.cpp/.h      ← シェーダーコンパイル・バインド
└── shaders/
    ├── common.hlsli       ← 共通の定義・関数
    ├── vertex.hlsl        ← Vertex Shader
    └── pixel.hlsl         ← Pixel Shader
```

---

## 確認チェックリスト

- [ ] D3D11 デバッグレイヤーでエラーなし
- [ ] RenderDoc でパイプライン全ステージが確認できる
- [ ] 定数バッファの `HLSL_PACK_RULE` を理解した（16バイトアライメント）
- [ ] Perspective Divide の動作を理解した
- [ ] UV の V 方向（D3D は上が 0、OpenGL は下が 0）を理解した

---

## 関連ドキュメント
- [../concepts/00-graphics-pipeline.md](../concepts/00-graphics-pipeline.md)
- [../concepts/03-vertex-transformation.md](../concepts/03-vertex-transformation.md)
- [02-lighting.md](02-lighting.md) - 次フェーズ
