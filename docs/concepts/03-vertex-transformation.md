# 03 - Vertex Transformation（頂点変換）

## 同次座標（Homogeneous Coordinates）

3D 空間の点 `(x, y, z)` を **4次元** `(x, y, z, w)` で表現する。

### w の意味
- `w = 1`: 通常の点（位置ベクトル）
- `w = 0`: 方向ベクトル（無限遠点）→ 平行移動の影響を受けない
- `w ≠ 1`: `(x/w, y/w, z/w)` に正規化することで3D座標を得る

```hlsl
float4 point     = float4(1, 2, 3, 1); // 点 → 平行移動 OK
float4 direction = float4(1, 0, 0, 0); // 方向 → 平行移動の影響なし

// 法線は方向ベクトルなので w=0
float4 normal = float4(0, 1, 0, 0);
```

### 平行移動を行列で表現できる理由
```
3×3 行列では平行移動を表現できない:
[1 0 0]   [x]   [x]
[0 1 0] × [y] = [y]  ← tx, ty を加えられない
[0 0 1]   [z]   [z]

4×4 同次座標なら可能:
[1 0 0 tx]   [x]   [x + tx]
[0 1 0 ty] × [y] = [y + ty]
[0 0 1 tz]   [z]   [z + tz]
[0 0 0  1]   [1]   [  1   ]
```

---

## Model 行列（Object → World Space）

### 変換の構成要素
1. **Scale（拡縮）**: `S(sx, sy, sz)`
2. **Rotation（回転）**: `Rx(θ)`, `Ry(θ)`, `Rz(θ)`
3. **Translation（平行移動）**: `T(tx, ty, tz)`

### 結合順序: `M = T × R × S`
```
モデル座標の頂点に対して:
1. まず Scale（原点基準で拡縮）
2. 次に Rotate（原点基準で回転）
3. 最後に Translate（移動）

逆順だと異なる結果になる！
```

### 各行列（D3D 左手系・Row-Major）
```hlsl
// Scale
float4x4 ScaleMatrix(float sx, float sy, float sz) {
    return float4x4(
        sx, 0,  0,  0,
        0,  sy, 0,  0,
        0,  0,  sz, 0,
        0,  0,  0,  1
    );
}

// Translation
float4x4 TranslationMatrix(float tx, float ty, float tz) {
    return float4x4(
        1, 0, 0, tx,
        0, 1, 0, ty,
        0, 0, 1, tz,
        0, 0, 0, 1
    );
}

// Rotation Y（D3D 左手系）
float4x4 RotationY(float angle) {
    float c = cos(angle), s = sin(angle);
    return float4x4(
         c, 0, s, 0,
         0, 1, 0, 0,
        -s, 0, c, 0,
         0, 0, 0, 1
    );
}
```

---

## View 行列（World → Camera Space）

### LookAt 行列の導出

カメラ設定: Eye（カメラ位置）, Target（注視点）, Up（上方向）

```hlsl
float4x4 LookAt(float3 eye, float3 target, float3 up) {
    float3 zAxis = normalize(target - eye);  // カメラ前方（D3D左手系: +Z）
    float3 xAxis = normalize(cross(up, zAxis));
    float3 yAxis = cross(zAxis, xAxis);

    // 回転（転置）+ 平行移動（負の内積）
    return float4x4(
        xAxis.x, xAxis.y, xAxis.z, -dot(xAxis, eye),
        yAxis.x, yAxis.y, yAxis.z, -dot(yAxis, eye),
        zAxis.x, zAxis.y, zAxis.z, -dot(zAxis, eye),
        0,       0,       0,        1
    );
}
```

**意味**: ワールドを「カメラが原点・-Z 方向が前」になるよう変換する

---

## Projection 行列

### Perspective Projection（透視投影）

```
fovY: 垂直方向の視野角（ラジアン）
aspect: アスペクト比（width / height）
near: 近クリップ面
far:  遠クリップ面
```

```hlsl
float4x4 PerspectiveFovLH(float fovY, float aspect, float nearZ, float farZ) {
    float yScale = 1.0 / tan(fovY * 0.5);
    float xScale = yScale / aspect;
    float Q = farZ / (farZ - nearZ); // D3D: 深度範囲 [0, 1]

    return float4x4(
        xScale, 0,      0,           0,
        0,      yScale, 0,           0,
        0,      0,      Q,           1,      // ← w に z を書き込む
        0,      0,      -Q * nearZ,  0
    );
}
```

透視投影後: `w = z_view`（カメラ空間の z 値）

### Orthographic Projection（平行投影）

```hlsl
float4x4 OrthoLH(float w, float h, float nearZ, float farZ) {
    return float4x4(
        2/w, 0,   0,              0,
        0,   2/h, 0,              0,
        0,   0,   1/(farZ-nearZ), 0,
        0,   0,   -nearZ/(farZ-nearZ), 1
    );
}
```

**用途**: Shadow Map 生成（指向性ライト）、UI 描画、CAD 視点

---

## MVP 結合変換と SV_Position

```hlsl
// CPU 側で結合（推奨：毎フレーム1回計算）
cbuffer PerObject : register(b0) {
    float4x4 mvpMatrix;   // Model × View × Projection
    float4x4 modelMatrix; // World Space 変換用に別途持つ
    float4x4 normalMatrix;
};

// VS での使用
VSOutput VSMain(VSInput input) {
    VSOutput output;

    // SV_Position: Clip 空間座標
    output.position = mul(mvpMatrix, float4(input.position, 1.0));

    // World 空間での位置（ライティング用）
    output.worldPos = mul(modelMatrix, float4(input.position, 1.0)).xyz;

    return output;
}
```

---

## 透視除算（Perspective Divide）

**GPU が自動的に実行**（VS のあと、RS の前）:

```
NDC.x = ClipPos.x / ClipPos.w
NDC.y = ClipPos.y / ClipPos.w
NDC.z = ClipPos.z / ClipPos.w
```

### なぜ遠くのものが小さく見えるか
- 透視投影行列は `ClipPos.w = view_z`（カメラからの深度）を書き込む
- 遠い頂点は `w` が大きい → 除算で x,y が小さくなる → 画面で小さく見える

### 属性の透視補正補間
ラスタライザはスクリーン空間でリニア補間するだけでは誤差が生じる。
`1/w` を使った透視補正補間を自動的に適用する。

```
補正補間: attr = (attr0/w0 * (1-t) + attr1/w1 * t) / (1/w0 * (1-t) + 1/w1 * t)
```

HLSL で `nointerpolation` セマンティクスを使うと補間を無効化できる。

---

## ビューポート変換

```hlsl
// D3D11: RSSetViewports で設定
D3D11_VIEWPORT vp;
vp.TopLeftX = 0.0f;
vp.TopLeftY = 0.0f;
vp.Width    = (float)width;
vp.Height   = (float)height;
vp.MinDepth = 0.0f;
vp.MaxDepth = 1.0f;
```

変換式:
```
Screen.x = NDC.x * (Width / 2) + (TopLeftX + Width / 2)
Screen.y = NDC.y * (-Height / 2) + (TopLeftY + Height / 2)  // Y 反転
Screen.z = NDC.z * (MaxDepth - MinDepth) + MinDepth
```

---

## クリッピングと Clip Planes

### 視錐台クリッピング（自動）
クリップ空間での条件: `-w ≤ x, y, z ≤ w`
範囲外のプリミティブは自動的にクリップされる。

### カスタム Clip Planes
```hlsl
// VS で SV_ClipDistance を出力
struct VSOutput {
    float4 position    : SV_Position;
    float  clipDist0   : SV_ClipDistance0; // 正値: 内側, 負値: 外側
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    output.position = mul(mvp, float4(input.position, 1.0));

    // 平面 (0,1,0,0) より上の頂点のみ描画
    float4 clipPlane = float4(0, 1, 0, 0);
    output.clipDist0 = dot(float4(worldPos, 1.0), clipPlane);

    return output;
}
```

**用途**: 水面の反射（水平面クリッピング）、ポータル描画

---

## 実装チェックリスト

- [ ] `float4(position, 1.0)` で w=1 を付加しているか（点として扱う）
- [ ] 法線は `w=0` または `float3x3` でキャストして変換しているか
- [ ] MVP の乗算順序: `P × V × M × vertex`（列ベクトル）または `vertex × M × V × P`（行ベクトル）
- [ ] CPU 側の行列が row-major / column-major 正しく設定されているか
- [ ] アスペクト比が実際のウィンドウサイズに追従しているか

---

## 関連ドキュメント
- [02-coordinate-spaces.md](02-coordinate-spaces.md) - 各空間の概要
- [04-normal-handling.md](04-normal-handling.md) - 法線変換
- [10-shadows.md](10-shadows.md) - ライト空間変換（Shadow Map）
