# 02 - Coordinate Spaces（座標空間）

## 座標変換フロー

```
Object Space（モデル空間）
        ↓ × Model Matrix
World Space（ワールド空間）
        ↓ × View Matrix
View/Camera Space（カメラ空間）
        ↓ × Projection Matrix
Clip Space（クリップ空間・同次座標）
        ↓ ÷ w（透視除算）
NDC（Normalized Device Coordinates）
        ↓ ビューポート変換
Screen Space（スクリーン空間）
```

---

## Object Space（モデル空間）

- メッシュ作成時の座標系（DCC ツールで定義）
- 原点はモデルの中心（またはデザイナーが決めた任意の点）
- 単位は任意（モデラーの設定に依存）
- **用途**: ノーマルマップのテクスチャ座標は Object Space を基準にすることがある

```hlsl
// VS 入力の position は Object Space
float3 objPos = input.position; // (0, 0, 0) が原点
```

---

## World Space（ワールド空間）

- シーン全体の共通座標系
- 全オブジェクトが同じ基準で配置される
- 光源・カメラ位置もここで定義
- **右手系 vs 左手系**: D3D は左手系（Z 正が画面奥）、OpenGL は右手系（Z 正が手前）

```hlsl
// Model Matrix で World Space に変換
float3 worldPos = mul(modelMatrix, float4(objPos, 1.0)).xyz;

// World Space での法線（法線行列が必要）
float3 worldNormal = normalize(mul((float3x3)normalMatrix, objNormal));
```

---

## View / Camera Space（カメラ空間）

- カメラを原点とした座標系
- カメラの向きが -Z 方向（D3D の場合）
- **用途**: フォグ計算、SSAO（スクリーン空間での深度比較）

```hlsl
// View Matrix でカメラ空間に変換
float4 viewPos = mul(viewMatrix, float4(worldPos, 1.0));
float  depth   = -viewPos.z; // D3D: カメラ前方 = -Z
```

### LookAt 行列の構成要素
- カメラ位置（Eye）: ワールド空間でのカメラ位置
- 注視点（Target）: カメラが見ている点
- 上方向（Up）: 通常 (0, 1, 0)

---

## Clip Space（クリップ空間）

- 同次座標（Homogeneous Coordinates）: `(x, y, z, w)`
- Projection Matrix 適用後の空間
- クリッピング（視錐台の外側を除去）はこの空間で行われる
- `SV_Position` セマンティクスの値がクリップ空間座標

```hlsl
float4 clipPos = mul(mvpMatrix, float4(objPos, 1.0));
// clipPos は (x, y, z, w) の同次座標
// 視錐台内: -w <= x,y,z <= w
```

### なぜ同次座標を使うか
- 平行移動を行列乗算で表現するため（3×3 では不可能）
- 透視投影の「除算」を後段で一括処理できる
- 無限遠点（w=0）を表現できる

---

## NDC（Normalized Device Coordinates）

- 透視除算（Perspective Divide）後の空間: `(x/w, y/w, z/w)`
- D3D: X∈[-1,1], Y∈[-1,1], Z∈[0,1]（深度は 0-1）
- OpenGL: X∈[-1,1], Y∈[-1,1], Z∈[-1,1]（深度は -1 to 1）

```
NDC.x = ClipPos.x / ClipPos.w
NDC.y = ClipPos.y / ClipPos.w  （D3D: Y は上が+1）
NDC.z = ClipPos.z / ClipPos.w  （D3D: 近平面=0, 遠平面=1）
```

**D3D と OpenGL の Y 軸の違い**:
- D3D: NDC.y が +1 → 画面上部
- OpenGL: NDC.y が +1 → 画面上部（同じ）
- ただしテクスチャの V 座標は逆（D3D: V=0 が上、OpenGL: V=0 が下）

---

## Screen Space（スクリーン空間）

- ピクセル座標系
- ビューポート変換で NDC → Screen Space

```
Screen.x = (NDC.x + 1) * ViewportWidth  / 2 + ViewportLeft
Screen.y = (1 - NDC.y) * ViewportHeight / 2 + ViewportTop  // Y 軸反転
Screen.z = NDC.z * (MaxDepth - MinDepth) + MinDepth
```

- **用途**: ポストプロセス、UI 描画、スクリーン空間エフェクト（SSAO、SSR）

---

## Tangent Space（タンジェント空間）

- サーフェスのノーマルマップに使用
- 詳細は [04-normal-handling.md](04-normal-handling.md) を参照

---

## 変換フロー まとめ

| 変換 | 行列 | 入力→出力 |
|---|---|---|
| Object → World | Model Matrix | objPos → worldPos |
| World → Camera | View Matrix | worldPos → viewPos |
| Camera → Clip | Projection Matrix | viewPos → clipPos |
| Clip → NDC | 透視除算（÷w） | clipPos → ndc |
| NDC → Screen | Viewport Transform | ndc → screenPos |

```hlsl
// VS での完全な変換（最小実装）
float4 VSMain(float3 objPos : POSITION) : SV_Position {
    float4 worldPos = mul(modelMatrix, float4(objPos, 1.0));
    float4 viewPos  = mul(viewMatrix,  worldPos);
    float4 clipPos  = mul(projMatrix,  viewPos);
    return clipPos; // SV_Position = Clip Space（透視除算はハードウェアが行う）
}

// または MVP をまとめた場合
float4 VSMain(float3 objPos : POSITION) : SV_Position {
    return mul(mvpMatrix, float4(objPos, 1.0));
}
```

---

## 各空間での演算

| 演算 | 推奨空間 | 理由 |
|---|---|---|
| ライティング計算 | World Space | 光源位置がワールドで定義されるため |
| ノーマルマップ | Tangent Space | テクスチャ格納が前提 |
| 深度比較 | View Space / NDC | カメラからの距離で比較 |
| ポストプロセス | Screen Space | ピクセル単位操作 |
| フォグ | View Space | カメラ距離で強度計算 |
| Shadow Map | Light Space | 光源からの深度 |

---

## 関連ドキュメント
- [03-vertex-transformation.md](03-vertex-transformation.md) - 各行列の詳細計算
- [04-normal-handling.md](04-normal-handling.md) - タンジェント空間
- [10-shadows.md](10-shadows.md) - ライト空間変換
