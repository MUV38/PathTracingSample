# USD Kitchenサンプル

`UsdKitchen` は、別プロジェクトでUSDから変換した `.ptscene` を読み、単一BLAS / TLASでDXR描画します。
OpenUSDのヘッダー、DLL、Pythonは表示アプリに不要です。ルートの `.slnx` はUSD SDKなしでビルドできます。
既存のWASDフリーカメラ、Alt回転・パン・ドリー、ホイールによる速度変更、撮影機能を利用できます。
F / Frame modelはシーン全体にカメラを合わせます。Exposureで表示を調整できます。

meshoptimizer v1.3の `meshopt_buildMeshletsSpatial` で作成したmeshletを読み込みます。
各meshletは最大64頂点・126三角形で、共有頂点の参照と8bitローカル三角形インデックスを持ちます。
上限は [NVIDIAのmeshlet推奨値](https://developer.nvidia.com/blog/introduction-turing-mesh-shaders/) を採用しています。これはMesh Shader向けの推奨値で、CLASでの最適サイズの保証ではありません。
1個のBLAS内にmeshletごとのindexed geometryを作り、Closest Hitでmeshletの頂点参照とローカルインデックスをGPU上でたどります。
通常DXR用の32bitインデックスはロード時に生成します。バイナリ内のローカル構造をそのままGPUにも保持し、将来のCLAS実装の入力として利用できます。
GeometryIndex()を使用するため、DXR Tier 1.1 / Shader Model 6.5が必要です。CLAS APIとMesh Shaderの実行はまだありません。

`Meshlet colors` にチェックするとmeshletごとに色分けします。頂点数とmeshlet数はパネルに表示します。
自動確認には `--meshlet-colors --screenshot <出力.png>` も使用できます。
同梱Preview Roomは192三角形、480共有頂点、9 meshletsです。
旧v1（三角形リスト）のバイナリは読み込みません。USDから新しい変換ツールで再変換してください。

初期表示はリポジトリ用に作成した **Preview Room** です。Kitchen Setとは異なる簡易モデルです。
ソースは `assets/Source/PreviewRoom/`、変換済みデータは `assets/PreviewRoom.ptscene` です。

## Kitchen Setを表示する

1. [公式ダウンロード一覧](https://openusd.org/release/dl_downloads.html) のKitchen Setを開き、利用者自身で条件を確認してダウンロードします。
2. ZIPを `assets/local/KitchenSet/` などへ展開します。インストール操作は不要です。参照先のUSDやテクスチャを含むフォルダ構造を維持してください。
3. [UsdConverter](../../tools/UsdConverter/README.md) を別ソリューションでビルドします。
4. 実際に展開された `Kitchen_set.usd` のパスを指定し、変換します。

```powershell
.\build\x64\Release\UsdConverter\UsdConverter.exe assets/local/KitchenSet/Kitchen_set.usd assets/local/KitchenSet/Kitchen.ptscene
.\build\x64\Release\UsdKitchen\UsdKitchen.exe --model assets/local/KitchenSet/Kitchen.ptscene
```

相対パスはカレントディレクトリ基準です。空白を含むパスは引用符で囲んでください。
Kitchen Setは同梱・自動取得しません。公式の利用条件に従い、入手データおよび変換したデータもコミットしないでください。
`assets/local/` と `build/` はGit管理外です。Kitchen Set実データでの変換・表示確認は未実施です。

## アプリから変換する

`Scene files` を開き、`Open USD...` でソースを選び、必要なら `Choose converter...` でビルド済み `UsdConverter.exe` を選び、`Convert and load` を押します。
変換は独立プロセスで実行します。既定では同じ構成の隣のUsdConverter、次にRelease構成を探します。
変換中も描画でき、キャンセル可能です。失敗時は現在のモデルを維持し、Conversion logにエラーを表示します。
変換結果とログは実行ファイル隣の `cache/` に保存します。必要に応じてアプリ終了後に削除してください。
`Open binary...` から既存バイナリも開けます。

```powershell
.\build\x64\Debug\UsdKitchen\UsdKitchen.exe --usd assets/Source/PreviewRoom/PreviewRoom.usda --converter "build/x64/Release/UsdConverter/UsdConverter.exe"
```

自動撮影では変換・ロードが終わってから撮影します。起動時の変換失敗は終了コード1と標準エラーで通知します。

```powershell
$app = Start-Process .\build\x64\Release\UsdKitchen\UsdKitchen.exe -ArgumentList '--usd "assets/Source/PreviewRoom/PreviewRoom.usda" --screenshot "build/checks/usd.png"' -Wait -PassThru
$app.ExitCode
```

## 直接照明

`Direct lighting` から1灯の方向光または点光源を選択します。方向光が初期設定です。

- 方向光: 方位角・仰角、色、照射強度を調整します。Headlightを有効にするとカメラ方向に追従します。
- 点光源: メートル単位のワールド位置、色、強度を調整します。光量は距離の二乗に反比例します。モデルを開くと光源位置をモデルの範囲に合わせます。
- Shadows: ヒット点から光源方向へRayQueryで遮蔽を検査します。点光源では光源までの区間だけを検査します。すべての三角形を不透明・両面の遮蔽物として扱うハードシャドウです。
- Shadow bias: 幾何法線方向のオフセットの最小値（m）。座標に比例するオフセットも加味し、自己交差を抑えます。非常に小さい形状では調整が必要です。
- Ambient fill: 影の部分の確認用に加える一定の補助光です。0で無効にできます。間接照明やGIではありません。

シェーディングは線形RGBの `albedo / pi` によるLambert拡散反射です。光の強度は方向光では相対的な照射強度、点光源では距離減衰前の相対強度として扱います。
蓄積した線形HDR放射輝度にExposureを適用し、ReinhardトーンマッピングとsRGB変換を行います。
USDのライトは読み込まず、このサンプル内で設定した光源を使用します。鏡面反射、面光源、ソフトシャドウは未実装です。

確認用の起動オプション:

```powershell
.\build\x64\Debug\UsdKitchen\UsdKitchen.exe --light point --screenshot build/checks/point.png
.\build\x64\Debug\UsdKitchen\UsdKitchen.exe --light directional --no-shadows --screenshot build/checks/no-shadows.png
.\build\x64\Debug\UsdKitchen\UsdKitchen.exe --ambient 0 --light-strength 0 --ibl-intensity 0 --screenshot build/checks/no-light.png
```

`--light-strength` は0～200、`--ambient` は0～2です。未指定の強度は方向光4、点光源40、補助光0です。
実装はDXR Tier 1.1 / SM6.5の既存要件内で、各ヒット点からインラインRayQueryを実行します。
追加のTraceRay再帰やシャドウ用のシェーダーテーブルは必要ありません。

## 間接照明とIBL

初期設定で3回の拡散バウンスを追跡します。各ヒット点で上記の直接照明を評価し、余弦重み付き半球サンプリングで次のレイを生成します。
Lambert BRDFとサンプリングPDFが打ち消し合うため、経路のスループットには各面のアルベドを乗じます。
一次レイ・反射後のレイともMiss ShaderでIBLテクスチャを参照します。IBLは背景にも、物体の照明にも使用します。
光源までの遮蔽レイのMissは可視判定だけに使い、IBLの寄与を二重に加算しません。

`Indirect lighting / IBL` の操作:

- Diffuse bounces: 拡散反射の回数（0～8、既定3）。0では物体は直接照明のみとなり、背景のIBL表示は維持します。
- Samples per frame: 1フレームあたりのサンプル数（1～16、既定1）。カメラレイの位置もジッターします。
- IBL intensity / rotation: 環境マップの強度（0～100、既定1）と水平方向の回転。
- Open IBL (.hdr): 緯度経度形式のRadiance HDRを選択します。Preview skyで自作の空マップに戻せます。
- Reset accumulation: 蓄積をやり直します。カメラ、光源、IBL、露出、バウンス数、サンプル数、描画領域、モデルが変わった場合も自動でリセットします。

既定のIBLはコードで生成した512×256の自作HDR空マップです。外部データや追加ソフトウェアの取得は不要です。
外部 `.hdr` はRGBE、modern scanline RLEまたは非圧縮、Y-majorの `-Y/+Y` と `+X/-X` に対応します。
画像は線形RGBとして読み込み、右手系Y-upのレイ方向から緯度経度UVを計算します。横方向はラップ、縦方向はクランプします。
最大16384画素／軸、合計16777216画素です。旧式のrepeat符号化、XYZE、EXR、キューブマップは未対応です。
FORMAT・解像度以外のHDRメタデータは適用しません。入手したHDRは `assets/local/` などGit管理外の場所に置いてください。

GPU上のRGBA32Fテクスチャに線形HDRの平均を蓄積し、表示時にトーンマッピングします。動かしている間は蓄積がリセットされるため、静止するとノイズが減ります。
現在は拡散反射と固定のバウンス上限によるパストレーシングです。IBLの重要度サンプリング、MIS、デノイズ、Russian roulette、鏡面・透過反射は未実装です。
小さく明るい光を含むHDRや閉じた空間では収束に多くのサンプルが必要です。

```powershell
.\build\x64\Release\UsdKitchen\UsdKitchen.exe --ibl "assets/local/studio.hdr" --spp 4 --samples 256 --screenshot build/checks/ibl.png
.\build\x64\Release\UsdKitchen\UsdKitchen.exe --light-strength 0 --samples 256 --screenshot build/checks/ibl-only.png
.\build\x64\Release\UsdKitchen\UsdKitchen.exe --ibl-intensity 0 --bounces 3 --samples 256 --screenshot build/checks/direct-bounces.png
```

`--samples` は自動撮影前に待つ最小蓄積サンプル数（1～65536）です。`--spp` の指定や撮影フレームの実行により指定数以上になる場合があります。
省略時の自動撮影は従来どおり数フレームの簡易確認です。間接照明の確認には `--samples 128` 以上を指定してください。
HDR読み込みのCPUテストはDebug / Releaseの `EnvironmentMapTests.exe build/checks` で実行できます。

参考: [PBRT: Infinite Area Lights](https://www.pbr-book.org/4ed/Light_Sources/Infinite_Area_Lights)

## 対応範囲

静的メッシュの形状確認用です。階層変換、参照・ペイロード、ネイティブインスタンス、凹多角形、法線、displayColor、定数のマテリアル色を変換します。
ステージ開始時刻をメートル単位、右手系Y-upの三角形に固定します。
サブディビジョンは制御ポリゴンのままです。テクスチャ、USDライト、透明度、シェーダーネットワーク、アニメーション、PointInstancerは未対応です。
色と拡散反射によるプレビューであり、USDの最終ルックを再現するものではありません。
CLASはまだ使用していません。
