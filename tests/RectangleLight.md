# 矩形面光源の検証

ルートの `PathTracingSample.slnx` をDebug / Releaseでビルドし、リポジトリ直下から実行します。

```powershell
.\tests\RunRectangleLight.ps1 -Configuration Debug
.\tests\RunRectangleLight.ps1 -Configuration Release
```

`WhiteFurnaceTests` の528条件を検査し、自作の床・遮蔽物・鏡モデルと白色HDRを生成して、UsdKitchenで9種類の画像を撮影します。
出力先はGit管理外の `build/checks/rectangle-<構成>/` です。`-OutputDirectory` で変更できます。
数値検証はDirectX 12 / SM6.0、表示はDXR Tier 1.1を使います。追加ソフトウェアやUSD SDKは不要です。
数値テストだけなら `WhiteFurnaceTests.exe <出力先> --warp` でWARPも使えます。

| 画像 | 確認する内容 |
| --- | --- |
| small / large | 0.2×0.2 m・放射輝度400と、2×2 m・放射輝度4。同じ面積×放射輝度で、ライトが大きくなると半影が広がる |
| mis / bsdf | 同じ1024サンプルで、NEE/MISの有無によるノイズ差と明るさの一致 |
| mirror / mirror-bsdf | 128サンプル。完全鏡に矩形が映り、MISの有無で反射を暗くしない |
| front / back | 上向き・下向きのライト。カメラから見た表側だけが発光する |
| back-ibl | 放射輝度0・白色IBL・反射0回。裏面も背景を遮る不透明面になる |

スクリプトは撮影終了コード、表示画素数、表側・鏡の高輝度画素、裏面が発光しないことを検査します。
画像右側60%以降を2画素間隔で調べ、MIS / BSDF画像の平均R値の差が1以内であることを要求します。
鏡と表側はR > 220の画素が200以上、裏側は0であることを要求します。各画像にR > 10の画素が500以上必要です。
`images.json` に集計とPNGパスを保存します。黒背景・白いモデル・白いライトの専用比較シーンです。
半影の形や不透明面の位置は画素数だけでは保証しないため、保存したPNGを開いて確認してください。
数値積分、解析解との比較、MIS有無の一致の誤差は [WhiteFurnace.md](WhiteFurnace.md) に記載しています。
マウス・キーボード操作の確認はこのスクリプトには含みません。

## CLI

| オプション | 内容 |
| --- | --- |
| --rect-light | 矩形を有効化。既定は無効 |
| --rect-position x y z | 中心位置（m）、各値-100000～100000。既定0, 2.5, 0 |
| --rect-size width height | 幅・高さ（m）、0.01～100。既定1×1 |
| --rect-orientation azimuth elevation | 方位角-180～180度、仰角-90～90度。既定0, -90（下向き） |
| --rect-color r g b | 線形RGB、各0～1。既定1, 0.96, 0.88 |
| --rect-radiance value | 放射輝度0～10000。既定10 |
| --no-rect-mis | 矩形のNEE/MISを無効にし、BRDFサンプリングのみで比較 |

矩形は独立した解析形状として描画し、`.ptscene` のモデルやUSD変換形式は変更しません。
不透明な両面の遮蔽物ですが、発光するのは法線側だけです。BRDF反射や透過は持ちません。
IBLは従来の設定で併用できます。IBLと矩形をそれぞれ1サンプル評価し、各光源へ到達したBRDF経路とのMISを別々に計算します。
反射回数上限では矩形NEEも打ち切りますが、そこまでに到達した発光面は表示します。
発光メッシュのNEE/MISは [別のテスト](EmissiveLights.md) で検証します。複数の解析的な矩形ライトとUSDライトの取り込みは未対応です。

参考: [PBRT Area Lights](https://pbr-book.org/4ed/Light_Sources/Area_Lights)、[PBRT A Better Path Tracer](https://pbr-book.org/4ed/Light_Transport_I_Surface_Reflection/A_Better_Path_Tracer)
