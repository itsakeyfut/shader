# 概念: Debug Rendering（デバッグレンダリング）

## 概要

ゲームエンジン開発において、デバッグ描画は**開発効率を直接左右する**重要なシステム。
物理コリジョン境界・AI 視野・骨格・ライト範囲・レンダリング統計などを視覚化することで、
バグの発見・チューニングが飛躍的に速くなる。

UE5 の `DrawDebugLine`・Unity の `Debug.DrawLine`・Godot の `draw_line` に相当するシステムを自作する。

---

## システム設計

### Immediate Mode API（推奨パターン）

```cpp
// 1フレームだけ描画される「即時」デバッグ描画API
// ゲームコードから自由に呼び出し、次フレームには消える

class DebugDraw {
public:
    // プリミティブ描画
    static void Line(float3 from, float3 to,
                     float4 color = float4(1,1,0,1), float duration = 0.0f);
    static void AABB(float3 min, float3 max,
                     float4 color = float4(0,1,0,1));
    static void Sphere(float3 center, float radius,
                       float4 color = float4(0,1,1,1), int segments = 16);
    static void Frustum(const Camera& cam, float4 color = float4(1,1,1,1));
    static void Axis(float3 origin, float3x3 rotation, float size = 1.0f);
    static void Arrow(float3 from, float3 dir, float4 color = float4(1,0,0,1));

    // ライフタイム管理（duration > 0 なら複数フレーム残る）
    static void LinePersistent(float3 from, float3 to,
                                float4 color, float duration);

    // テキスト（スクリーンスペース or ワールドスペース）
    static void Text(float3 worldPos, const char* fmt, ...);
    static void TextScreen(float2 screenPos, const char* fmt, ...);
};
```

### 内部実装（動的頂点バッファ）

```cpp
// 毎フレーム再構築する動的頂点バッファ
struct DebugVertex {
    float3 position;
    float4 color;
};

class DebugDrawSystem {
    static constexpr int MAX_VERTS = 1 << 20; // 100万頂点

    // ダブルバッファリング（CPU が書き、GPU が読む）
    ID3D12Resource*  m_vertexBuffer[FRAME_BUFFER_COUNT];
    DebugVertex*     m_mappedPtr[FRAME_BUFFER_COUNT];
    int              m_vertCount;

    // 線分（2頂点）を追加
    void AddLine(float3 from, float3 to, float4 color) {
        if (m_vertCount + 2 > MAX_VERTS) return;
        int frame = g_frameIndex % FRAME_BUFFER_COUNT;
        m_mappedPtr[frame][m_vertCount++] = {from, color};
        m_mappedPtr[frame][m_vertCount++] = {to,   color};
    }

    // フレーム末に D3D12_PRIMITIVE_TOPOLOGY_LINELIST でドロー
    void Flush(ID3D12GraphicsCommandList* cmd) {
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
        cmd->DrawInstanced(m_vertCount, 1, 0, 0);
        m_vertCount = 0;
    }
};
```

---

## プリミティブ描画の実装

### AABB（Axis-Aligned Bounding Box）

```cpp
void DebugDraw::AABB(float3 min, float3 max, float4 color) {
    // 12本の辺 = 8頂点の組み合わせ
    float3 corners[8] = {
        {min.x, min.y, min.z}, {max.x, min.y, min.z},
        {max.x, max.y, min.z}, {min.x, max.y, min.z},
        {min.x, min.y, max.z}, {max.x, min.y, max.z},
        {max.x, max.y, max.z}, {min.x, max.y, max.z},
    };
    // 底面 4 辺
    Line(corners[0], corners[1], color);
    Line(corners[1], corners[2], color);
    Line(corners[2], corners[3], color);
    Line(corners[3], corners[0], color);
    // 上面 4 辺
    Line(corners[4], corners[5], color);
    Line(corners[5], corners[6], color);
    Line(corners[6], corners[7], color);
    Line(corners[7], corners[4], color);
    // 縦 4 辺
    for (int i = 0; i < 4; i++)
        Line(corners[i], corners[i + 4], color);
}
```

### 球（Sphere Wireframe）

```cpp
void DebugDraw::Sphere(float3 center, float radius, float4 color, int segments) {
    // XY / YZ / XZ の 3 平面で円を描画
    auto DrawCircle = [&](float3 normal) {
        float3 right = abs(dot(normal, float3(0,1,0))) < 0.99f
                     ? normalize(cross(float3(0,1,0), normal))
                     : float3(1,0,0);
        float3 up    = cross(normal, right);
        float3 prev  = center + right * radius;
        for (int i = 1; i <= segments; i++) {
            float  angle = (2.0f * PI * i) / segments;
            float3 curr  = center + (right * cosf(angle) + up * sinf(angle)) * radius;
            Line(prev, curr, color);
            prev = curr;
        }
    };
    DrawCircle({1,0,0});
    DrawCircle({0,1,0});
    DrawCircle({0,0,1});
}
```

### 座標軸（Axis Gizmo）

```cpp
void DebugDraw::Axis(float3 origin, float3x3 rotation, float size) {
    Arrow(origin, rotation[0] * size, float4(1, 0, 0, 1)); // X: 赤
    Arrow(origin, rotation[1] * size, float4(0, 1, 0, 1)); // Y: 緑
    Arrow(origin, rotation[2] * size, float4(0, 0, 1, 1)); // Z: 青
}

void DebugDraw::Arrow(float3 from, float3 dir, float4 color) {
    float3 to = from + dir;
    Line(from, to, color);

    // 矢印の先端（コーン形状を 4 本の線で近似）
    float  headLen = length(dir) * 0.2f;
    float3 n       = normalize(dir);
    float3 side    = normalize(cross(n, abs(n.y) < 0.99f ?
                                float3(0,1,0) : float3(1,0,0)));
    float3 up      = cross(n, side);
    Line(to, to - n * headLen + side * headLen * 0.5f, color);
    Line(to, to - n * headLen - side * headLen * 0.5f, color);
    Line(to, to - n * headLen + up   * headLen * 0.5f, color);
    Line(to, to - n * headLen - up   * headLen * 0.5f, color);
}
```

### 錐台（Camera Frustum）

```cpp
void DebugDraw::Frustum(const Camera& cam, float4 color) {
    // 逆 VP 行列でクリップ空間の 8 頂点をワールドへ逆変換
    float4x4 invVP = inverse(cam.viewProj);
    float3 corners[8];
    int idx = 0;
    for (float z : {0.0f, 1.0f})
    for (float y : {-1.0f, 1.0f})
    for (float x : {-1.0f, 1.0f}) {
        float4 clip = float4(x, y, z, 1);
        float4 world = invVP * clip;
        corners[idx++] = world.xyz / world.w;
    }
    // Near Plane 4 辺
    AABB_from_corners(corners, color); // 適宜
}
```

---

## 深度テスト制御（Always-On-Top）

デバッグ描画には「常に最前面に表示」モードと「深度あり」モードの両方が必要。

```cpp
// 2パスで描画:
// Pass 1: DepthTest=LESS (深度あり, 3D オブジェクトとして)
// Pass 2: DepthTest=ALWAYS (常に最前面, 半透明で重ねる)

// D3D11 例:
// Pass 1
depthStencilStateNormal->bind();
debugDrawSystem.Flush(cmdList, DepthMode::Normal);

// Pass 2 (Always-on-top: 暗めの半透明で描画)
depthStencilStateAlways->bind();
blendStateAlpha->bind();
debugDrawSystem.FlushXRay(cmdList); // XRay モードの頂点をまとめて描画
```

### デバッグシェーダー

```hlsl
// Debug Draw 用 Vertex Shader
struct DebugVSInput {
    float3 position : POSITION;
    float4 color    : COLOR;
};
struct DebugVSOutput {
    float4 svPos : SV_Position;
    float4 color : COLOR;
};

cbuffer DebugCB : register(b0) {
    float4x4 viewProj;
};

DebugVSOutput DebugVS(DebugVSInput input) {
    DebugVSOutput output;
    output.svPos = mul(viewProj, float4(input.position, 1.0));
    output.color = input.color;
    return output;
}

float4 DebugPS(DebugVSOutput input) : SV_Target {
    return input.color;
}
```

---

## 骨格（Skeleton）可視化

```cpp
// SkeletonDebugDraw: ボーン配列からボーン間の線分を描画
void DrawSkeleton(const std::vector<Bone>& bones,
                  const std::vector<float4x4>& worldMatrices) {
    for (int i = 0; i < bones.size(); i++) {
        int parent = bones[i].parentIndex;
        if (parent < 0) continue;

        float3 childPos  = float3(worldMatrices[i][3]);
        float3 parentPos = float3(worldMatrices[parent][3]);

        // ボーンの長さで色変化（長いほど黄→赤）
        float len   = length(childPos - parentPos);
        float4 col  = float4(min(len, 1.0f), max(1.0f - len, 0.0f), 0.0f, 1.0f);

        DebugDraw::Line(parentPos, childPos, col);
        DebugDraw::Sphere(childPos, 0.02f, float4(1, 1, 0, 1));
    }
}
```

---

## デバッグオーバーレイモード

シーン全体のデバッグ情報を可視化するオーバーレイ。UE5 の「ビューモード」に相当。

```hlsl
// デバッグビューモード選択用定数バッファ
cbuffer DebugViewMode : register(b5) {
    int debugMode;
    // 0: 通常, 1: Albedo, 2: WorldNormal, 3: Roughness,
    // 4: Metalness, 5: AO, 6: Depth, 7: Wireframe
};

float4 GBufferDebugPS(float2 uv : TEXCOORD0) : SV_Target {
    GBufferData gbuf = DecodeGBuffer(uv);
    switch (debugMode) {
        case 1: return float4(gbuf.albedo, 1);
        case 2: return float4(gbuf.normal * 0.5 + 0.5, 1);
        case 3: return gbuf.roughness.xxxx;
        case 4: return gbuf.metalness.xxxx;
        case 5: return gbuf.ao.xxxx;
        case 6: {
            float depth = depthTex.Sample(pointSamp, uv).r;
            float linear = LinearizeDepth(depth);
            return float4(linear / farClip, 0, 0, 1);
        }
        default: return float4(0, 0, 0, 0); // 通常描画へ
    }
}
```

---

## 統計オーバーレイ（GPU タイム / Draw Call 数）

```cpp
// タイムスタンプクエリでパスごとの GPU 時間を計測
struct GPUTimestamp {
    std::string name;
    UINT64      begin;
    UINT64      end;
};

// D3D12 Timestamp Query
void BeginTimestamp(const char* name, ID3D12GraphicsCommandList* cmd) {
    int idx = m_timestampIdx++;
    m_names[idx] = name;
    cmd->EndQuery(m_queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, idx * 2);
}
void EndTimestamp(ID3D12GraphicsCommandList* cmd) {
    int idx = m_timestampIdx - 1;
    cmd->EndQuery(m_queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, idx * 2 + 1);
}

// 結果を ImGui（または自前テキスト描画）でオーバーレイ表示
void DrawGPUTimings() {
    for (auto& ts : m_timestamps)
        ImGui::Text("%-30s  %.2f ms", ts.name.c_str(),
                    (ts.end - ts.begin) / m_gpuFrequency * 1000.0);
}
```

---

## ワールドスペーステキスト描画

```cpp
// ビルボードテキスト: ワールド座標をスクリーンへ投影してテキスト描画
void DrawWorldText(float3 worldPos, const char* text, float4 color) {
    float4 clip    = viewProj * float4(worldPos, 1);
    if (clip.w <= 0) return;  // カメラ後方

    float2 ndc     = clip.xy / clip.w;
    float2 screen  = (ndc * float2(0.5, -0.5) + 0.5) * screenSize;

    DrawScreenText(screen, text, color);
}
```

---

## 関連ドキュメント

- [00-graphics-pipeline.md](00-graphics-pipeline.md) — パイプラインとステート
- [03-vertex-transformation.md](03-vertex-transformation.md) — 座標変換（ワールド→スクリーン）
- [roadmap/15-debug-rendering.md](../roadmap/15-debug-rendering.md) — デバッグ描画実装ロードマップ
- [roadmap/11-render-graph.md](../roadmap/11-render-graph.md) — Render Graph との統合
