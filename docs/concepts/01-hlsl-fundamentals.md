# 01 - HLSL Fundamentals

## スカラー / ベクトル / 行列型

### スカラー型
```hlsl
bool   b = true;
int    i = -1;
uint   u = 42u;
float  f = 3.14;
half   h = 1.0h;   // 16-bit float（モバイル向け）
double d = 1.0;    // 64-bit（計算シェーダーで使用）
```

### ベクトル型
```hlsl
float2 uv   = float2(0.5, 0.5);
float3 pos  = float3(1.0, 0.0, 0.0);
float4 color = float4(1.0, 0.0, 0.0, 1.0);

// スウィズル（swizzle）
float3 rgb = color.rgb;   // color.xyz と同じ
float2 xy  = pos.xy;
float  r   = color.r;     // color.x と同じ

// スウィズルで並べ替え
float4 bgra = color.bgra;
float3 zzz  = pos.zzz;
```

### 行列型
```hlsl
float4x4 mvp;       // 4×4 行列（最も一般的）
float3x3 normalMat; // 3×3（法線変換用）
float4x3 world;     // 非正方行列も可能

// 行列要素アクセス
float m00 = mvp[0][0];  // 行0・列0

// 行列・ベクトル乗算（2通り）
float4 clipPos = mul(mvp, float4(pos, 1.0));  // 行列 × 列ベクトル（HLSL 標準）
float4 clipPos2 = mul(float4(pos, 1.0), mvp); // 行ベクトル × 行列（転置した場合）
```

**注意**: HLSL の行列はデフォルトで **row-major** 記述だが、
`cbuffer` に渡す際は `column_major` キーワードまたは CPU 側でのコンパイルフラグに注意。

---

## セマンティクス

セマンティクスはデータの「用途」を GPU に伝えるラベル。

### 入力セマンティクス（IA → VS）
```hlsl
struct VSInput {
    float3 position : POSITION;   // 頂点位置
    float3 normal   : NORMAL;     // 法線
    float2 uv       : TEXCOORD0;  // UV座標
    float4 color    : COLOR0;     // 頂点カラー
    float4 tangent  : TANGENT;    // タンジェント
};
```

### システム値セマンティクス（SV_xxx）
```hlsl
// VS 出力の必須セマンティクス
float4 position : SV_Position; // クリップ空間座標（ラスタライザが使用）

// PS 入力
float4 screenPos : SV_Position; // スクリーン座標（ピクセル位置）
bool   isFront   : SV_IsFrontFace; // 表面かどうか

// PS 出力
float4 color0 : SV_Target0; // レンダーターゲット 0 への出力
float4 color1 : SV_Target1; // MRT 用ターゲット 1
float  depth  : SV_Depth;   // 深度値上書き

// CS
uint3 dispatchId  : SV_DispatchThreadID; // グローバルスレッドID
uint3 groupId     : SV_GroupID;          // グループID
uint3 groupThread : SV_GroupThreadID;    // グループ内スレッドID
uint  groupIndex  : SV_GroupIndex;       // グループ内フラットインデックス
```

### 頂点シェーダー出力 / ピクセルシェーダー入力
```hlsl
struct VSOutput {
    float4 position : SV_Position;
    float3 worldPos : TEXCOORD0;  // SV_ でないセマンティクスはラスタライザが補間
    float3 normal   : TEXCOORD1;
    float2 uv       : TEXCOORD2;
};
```

---

## 組み込み関数

### 数学
```hlsl
// 内積・外積
float  d = dot(a, b);        // |a||b|cos(θ) → ライティングの基本
float3 c = cross(a, b);      // 法線計算、面法線

// ベクトル演算
float3 n = normalize(v);     // 正規化
float  l = length(v);        // 長さ
float  d2 = dot(v, v);       // 長さの二乗（sqrt不要で高速）

// クランプ・補間
float s = saturate(x);       // clamp(x, 0.0, 1.0)
float c = clamp(x, lo, hi);
float r = lerp(a, b, t);     // a*(1-t) + b*t
float r = smoothstep(e0, e1, x); // Hermite 補間

// 冪乗・指数
float p = pow(x, n);         // x^n（x > 0 が安全）
float e = exp(x);            // e^x
float e2 = exp2(x);          // 2^x
float s = sqrt(x);
float rs = rsqrt(x);         // 1/sqrt(x)（高速逆数平方根）

// 三角関数
float s = sin(x);            // ラジアン
float c = cos(x);
float t = tan(x);
float2 sc = sincos(x, s, c); // sin と cos を同時計算（効率的）

// 絶対値・符号
float a = abs(x);
float s = sign(x);           // -1, 0, 1

// 床・天井・丸め
float f = floor(x);
float c = ceil(x);
float r = round(x);
float fr = frac(x);          // 小数部分（x - floor(x)）
float m = fmod(x, y);        // 剰余

// 最大・最小
float mx = max(a, b);
float mn = min(a, b);
```

### テクスチャサンプリング
```hlsl
Texture2D    tex   : register(t0);
SamplerState samp  : register(s0);

// PS 内でのサンプリング
float4 color = tex.Sample(samp, uv);              // バイリニア補間
float4 color = tex.SampleLevel(samp, uv, mip);   // ミップレベル指定
float4 color = tex.SampleBias(samp, uv, bias);   // ミップバイアス調整
float4 color = tex.Load(int3(x, y, mip));        // 直接ピクセルアクセス

// CS / GS 内（微分なし → SampleLevel 使用）
float4 color = tex.SampleLevel(samp, uv, 0.0);
```

### 行列演算
```hlsl
float4x4 t = transpose(m);      // 転置
float4x4 inv = inverse(m);      // 逆行列（HLSL には組み込みなし、自前実装が必要）
float4 v = mul(mat, vec);       // 行列×ベクトル
float4x4 r = mul(m1, m2);       // 行列×行列
```

### その他
```hlsl
// 条件分岐（HLSL は branch/flatten ヒント）
[branch]  if (condition) { ... }  // 実際の分岐命令
[flatten] if (condition) { ... }  // 両パスを評価して select

// デバッグ用（RenderDoc で確認可能）
// clip(x): x < 0 ならピクセルを破棄
clip(alpha - 0.5);  // アルファカットアウト

// ddx / ddy: 隣接ピクセルとの微分（PS のみ）
float2 dx = ddx(uv);
float2 dy = ddy(uv);
```

---

## レジスタ

| プレフィックス | 用途 | HLSL キーワード |
|---|---|---|
| `b` | 定数バッファ | `cbuffer` / `ConstantBuffer<T>` |
| `t` | シェーダーリソース | `Texture2D`, `Buffer`, `StructuredBuffer` |
| `s` | サンプラー | `SamplerState`, `SamplerComparisonState` |
| `u` | UAV（読み書き） | `RWTexture2D`, `RWStructuredBuffer` |

```hlsl
cbuffer PerFrame : register(b0) {
    float4x4 viewProj;
    float3   cameraPos;
    float    time;
};

Texture2D    albedoMap  : register(t0);
Texture2D    normalMap  : register(t1);
SamplerState linearSamp : register(s0);

RWTexture2D<float4> outputTex : register(u0);  // CS での書き込み
```

---

## シェーダープロファイル

| プロファイル | API | 機能 |
|---|---|---|
| `vs_5_0` / `ps_5_0` | D3D11 Feature Level 11.0 | 基本的なシェーダー |
| `cs_5_0` | D3D11 | Compute Shader |
| `vs_6_0` / `ps_6_0` | D3D12 / Shader Model 6.0 | Wave Intrinsics |
| `cs_6_5` | D3D12 Ultimate | Mesh Shader, DXR |
| `lib_6_3` | D3D12 | Ray Tracing ライブラリ |

---

## コンパイル: fxc / dxc

### fxc（レガシー、SM 5.x まで）
```bash
fxc /T vs_5_0 /E VSMain /Fo shader.vs.cso shader.hlsl
fxc /T ps_5_0 /E PSMain /Fo shader.ps.cso shader.hlsl

# デバッグ情報付き
fxc /T ps_5_0 /E PSMain /Zi /Od /Fo shader.ps.cso shader.hlsl
```

### dxc（DirectX Shader Compiler、SM 6.x）
```bash
dxc -T vs_6_0 -E VSMain -Fo shader.vs.dxil shader.hlsl
dxc -T ps_6_0 -E PSMain -Fo shader.ps.dxil shader.hlsl

# SPIR-V 出力（Vulkan 向け）
dxc -T ps_6_0 -E PSMain -spirv -Fo shader.spv shader.hlsl
```

### CMake での統合例
```cmake
function(compile_shader TARGET SOURCE ENTRY TYPE)
    set(OUTPUT "${CMAKE_BINARY_DIR}/shaders/${SOURCE}.${TYPE}.cso")
    add_custom_command(
        OUTPUT ${OUTPUT}
        COMMAND fxc /T ${TYPE}_5_0 /E ${ENTRY} /Fo ${OUTPUT} ${SOURCE}
        DEPENDS ${SOURCE}
    )
    target_sources(${TARGET} PRIVATE ${OUTPUT})
endfunction()
```

---

## 関連ドキュメント
- [00-graphics-pipeline.md](00-graphics-pipeline.md) - パイプライン全体
- [03-vertex-transformation.md](03-vertex-transformation.md) - MVP 変換の実装
- [15-compute-shaders.md](15-compute-shaders.md) - Compute Shader 詳細
