# Roadmap: Phase 15 - Debug Rendering（デバッグ描画システム）

**プロジェクト**: 各プロジェクトに統合するライブラリ（`D:/dev/shader/debug-rendering/`）
**API**: Direct3D 11 / 12 共通（薄い抽象レイヤー）
**目標**: 全フェーズで使い回せるデバッグ描画ライブラリを構築し、ゲームエンジン開発の基盤を固める

> **注**: このフェーズは Phase 1 完了後の早い段階（Phase 2〜3 と並行）で着手することを推奨する。
> 以降のすべてのフェーズでデバッグ描画が開発効率を大幅に向上させる。

---

## 習得する概念

| 概念 | 関連ドキュメント |
|---|---|
| デバッグ描画設計 | [22-debug-rendering.md](../concepts/22-debug-rendering.md) |
| GPU パイプライン | [00-graphics-pipeline.md](../concepts/00-graphics-pipeline.md) |
| 座標変換 | [03-vertex-transformation.md](../concepts/03-vertex-transformation.md) |

---

## フェーズ分け

### フェーズ 15-1: Line / AABB / Sphere の基本描画

**実装項目**:
- `DebugDrawSystem` クラス設計（D3D11 / D3D12 共通インターフェース）
- Upload 動的頂点バッファ（毎フレーム CPU から書き込み）
- 最小シェーダー（DebugVS / DebugPS：位置 + カラーのみ）
- `LINELIST` トポロジーで描画
- 基本 API: `Line`, `AABB`, `Sphere`, `Axis`
- ダブルバッファリング（D3D12 の場合は `UPLOAD` ヒープ × フレーム数）

```cpp
// 最小実装
struct DebugVertex {
    float x, y, z;
    uint32_t color; // RGBA パック
};

class DebugDrawSystem {
    static const int MAX_VERTS = 1 << 20; // ~12MB
    DebugVertex* m_ptr;   // マップ済みポインタ
    int          m_count; // 現フレームの頂点数

public:
    void Line(XMFLOAT3 a, XMFLOAT3 b, XMFLOAT4 col) {
        m_ptr[m_count++] = {a.x, a.y, a.z, PackColor(col)};
        m_ptr[m_count++] = {b.x, b.y, b.z, PackColor(col)};
    }
    void AABB(XMFLOAT3 mn, XMFLOAT3 mx, XMFLOAT4 col);
    void Sphere(XMFLOAT3 c, float r, XMFLOAT4 col, int seg=16);
    void Flush(/* cmd list */);
};
```

---

### フェーズ 15-2: 深度テスト制御（Always-On-Top と Normal の 2 パス）

**実装項目**:
- Normal モード：`DEPTH_LESS`（3D オブジェクトとして遮蔽される）
- XRay モード：`DEPTH_ALWAYS`（常に前面に表示、半透明 0.4 alpha）
- 各プリミティブに `DebugMode::Normal / XRay / Both` フラグを追加
- ブレンドステート設定（XRay 用にアルファブレンド有効化）

```hlsl
// Debug VS / PS（最小）
cbuffer DebugCB : register(b0) { float4x4 viewProj; };

struct VSIn  { float3 pos : POSITION; float4 col : COLOR; };
struct VSOut { float4 sv  : SV_Position; float4 col : COLOR; };

VSOut DebugVS(VSIn v) {
    VSOut o;
    o.sv  = mul(float4(v.pos, 1), viewProj);
    o.col = v.col;
    return o;
}
float4 DebugPS(VSOut v) : SV_Target { return v.col; }
```

---

### フェーズ 15-3: Arrow / Frustum / Ray / Capsule の追加

**実装項目**:
- `Arrow`：矢印ヘッド付き方向ベクトル表示（AI 視野方向・速度ベクトルに使用）
- `Frustum`：カメラの InvVP から 8 頂点を逆変換して描画
- `Ray`：原点 + 方向 + 長さ（レイキャスト結果の可視化）
- `Capsule`：2 円 + 側面の線分（物理コライダー可視化）
- `OBB`：回転行列 + 半サイズから 8 頂点を計算して AABB 同様に描画

```cpp
void DebugDrawSystem::Frustum(XMMATRIX invVP, XMFLOAT4 col) {
    // NDC 8 頂点を逆変換
    float corners[8][4] = {
        {-1,-1, 0,1}, { 1,-1, 0,1}, { 1, 1, 0,1}, {-1, 1, 0,1}, // Near
        {-1,-1, 1,1}, { 1,-1, 1,1}, { 1, 1, 1,1}, {-1, 1, 1,1}, // Far
    };
    XMFLOAT3 pts[8];
    for (int i = 0; i < 8; i++) {
        XMVECTOR v = XMVector4Transform(XMLoadFloat4((XMFLOAT4*)corners[i]), invVP);
        v = XMVectorScale(v, 1.0f / XMVectorGetW(v));
        XMStoreFloat3(&pts[i], v);
    }
    // Near 4 辺
    for (int i = 0; i < 4; i++) Line(pts[i], pts[(i+1)%4], col);
    // Far 4 辺
    for (int i = 4; i < 8; i++) Line(pts[i], pts[4+(i-4+1)%4], col);
    // 縦 4 辺
    for (int i = 0; i < 4; i++) Line(pts[i], pts[i+4], col);
}
```

---

### フェーズ 15-4: 骨格（Skeleton）可視化

**実装項目**:
- `SkeletonDebugDraw`: ボーン配列 + World 行列から骨格を描画
- 各ジョイントに小球、ボーン間に線分
- 選択ボーンをハイライト（黄色）
- ボーン名のテキストラベル（オプション）

```cpp
void DrawSkeleton(const std::vector<int>& parents,
                  const std::vector<XMMATRIX>& worldMats,
                  int selectedBone = -1) {
    for (int i = 0; i < (int)parents.size(); i++) {
        XMFLOAT3 pos;
        XMStoreFloat3(&pos, worldMats[i].r[3]);
        bool sel = (i == selectedBone);

        Draw::Sphere(pos, 0.02f, sel ? YELLOW : WHITE);

        if (parents[i] >= 0) {
            XMFLOAT3 parentPos;
            XMStoreFloat3(&parentPos, worldMats[parents[i]].r[3]);
            Draw::Line(parentPos, pos, sel ? YELLOW : CYAN);
        }
        Draw::Axis(pos, ToFloat3x3(worldMats[i]), 0.05f);
    }
}
```

---

### フェーズ 15-5: G-Buffer デバッグオーバーレイモード

**実装項目**:
- `DebugViewMode` 定数バッファ（0=通常, 1=Albedo, 2=Normal, 3=Roughness...）
- デバッグシェーダーパスを Final Composite の前後に切り替える仕組み
- RenderDoc / PIX とシームレスに連携（パスに `PIXBeginEvent` を設置）
- `ImGui` ドロップダウンでモード切り替え（または数字キーショートカット）

```hlsl
// 合成 PS でモード切り替え
float4 FinalCompositePS(float2 uv : TEXCOORD0) : SV_Target {
    if (debugViewMode == 0) {
        return float4(ACESFilmic(hdrBuffer.Sample(s, uv).rgb * exposure), 1);
    }

    GBufferData g = DecodeGBuffer(uv);
    switch (debugViewMode) {
        case 1:  return float4(g.albedo, 1);
        case 2:  return float4(g.worldNormal * 0.5 + 0.5, 1);
        case 3:  return g.roughness.xxxx;
        case 4:  return g.metalness.xxxx;
        case 5:  return g.ao.xxxx;
        case 6:  return float4(frac(g.worldPos * 0.1), 1); // World Grid
        case 7:  {
            float d = LinearizeDepth(depthTex.Sample(s, uv).r) / farClip;
            return float4(d, d, d, 1);
        }
        default: return float4(1, 0, 1, 1); // マゼンタ：未定義モード
    }
}
```

---

### フェーズ 15-6: GPU タイミング・統計オーバーレイ

**実装項目**:
- D3D12 `ID3D12QueryHeap`（`D3D12_QUERY_TYPE_TIMESTAMP`）でパス別 GPU 時間を計測
- `DrawCall 数`, `Triangle 数`, `GPU 時間` を ImGui テーブルで表示
- GPU バジェット超過（> 16.6ms）を赤色でハイライト
- CPU フレーム時間との比較表示（ボトルネック判定）

```cpp
// TimestampQueryPool
class GPUTimer {
    ID3D12QueryHeap*  m_heap;
    ID3D12Resource*   m_readback;
    UINT64            m_frequency;

public:
    void Begin(const char* name, ID3D12GraphicsCommandList* cmd);
    void End(ID3D12GraphicsCommandList* cmd);
    void Resolve(ID3D12GraphicsCommandList* cmd); // フレーム末に呼ぶ
    void Display(); // ImGui で表示

    // 使用例:
    // timer.Begin("GBuffer", cmd);
    // DrawGBuffer(cmd);
    // timer.End(cmd);
};
```

---

### フェーズ 15-7: ワールドスペーステキスト（ビルボードラベル）

**実装項目**:
- `DrawWorldText(float3 pos, const char* text)`
- ワールド座標 → スクリーン座標への投影
- ImGui `DrawList->AddText()` または自前ビットマップフォントレンダラーで描画
- 距離に応じたフェードアウト（遠すぎると消える）

---

## ファイル構成

```
debug-rendering/
├── include/
│   └── DebugDraw.h         ← 公開 API（static 関数群）
├── src/
│   ├── DebugDrawSystem.cpp  ← 頂点バッファ・フラッシュ
│   ├── DebugShapes.cpp      ← Line/AABB/Sphere/Arrow/Frustum/Capsule
│   ├── DebugSkeleton.cpp    ← 骨格描画
│   ├── DebugOverlay.cpp     ← G-Buffer モード・タイミング
│   └── GPUTimer.cpp         ← タイムスタンプクエリ
└── shaders/
    ├── debug_vs.hlsl
    ├── debug_ps.hlsl
    └── debug_overlay_ps.hlsl
```

ライブラリは `CMakeLists.txt` で `STATIC` ライブラリとしてビルドし、
他のプロジェクト（Phase 2〜14）から `target_link_libraries` で参照する。

---

## 確認チェックリスト

- [ ] AABB / 球が正しい位置に描画される
- [ ] XRay モードで壁越しにコライダーが見える
- [ ] カメラの錐台が 8 辺正確に描かれる
- [ ] ボーン骨格がキャラクターにオーバーレイされる
- [ ] G-Buffer デバッグモードで法線・ラフネスが確認できる
- [ ] GPU タイミングで各パスの時間が ms 単位で読み取れる

---

## 関連ドキュメント
- [14-global-illumination.md](14-global-illumination.md) - 前フェーズ
- [16-advanced-shadows.md](16-advanced-shadows.md) - 次フェーズ
- [../concepts/22-debug-rendering.md](../concepts/22-debug-rendering.md)
