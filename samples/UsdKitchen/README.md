# USD Kitchenサンプル

`UsdKitchen` は、別プロジェクトでUSDから変換した `.ptscene` を読み、単一BLAS / TLASでDXR描画します。
OpenUSDのヘッダー、DLL、Pythonは表示アプリに不要です。ルートの `.slnx` はUSD SDKなしでビルドできます。
Streamline 2.14.16のヘッダーは `tools/RestoreStreamline.ps1` で復元します。任意のDLSS Ray Reconstruction（ネイティブ解像度／DLAA）は `--rr` またはUIから有効にできます。[SDKと実行DLLの導入・比較・検証](../../tests/RayReconstruction.md) を参照してください。
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
マテリアルテーブルを含むv3形式を読み込みます。旧v1 / v2は未対応のためUSDから再変換してください。

初期表示は `assets/local/Kitchen_set.ptscene` があればKitchen Setを優先します。通常版がなく `assets/local/Kitchen_set_instanced.ptscene` だけがある場合はそちらを使います。
どちらもなければ、リポジトリ用に作成した **Preview Room** を表示します。Kitchen Setとは異なる簡易モデルです。
通常利用の変換済みデータは `assets/local/` へ保存してください。`--model` / `--usd` の明示指定は既定モデルより優先します。
既定の探索は実行ファイルからリポジトリの `.slnx` を見つけて `assets/` を使うため、Visual Studioや別のカレントディレクトリから起動しても同じモデルを選びます。リポジトリ外へ配布する場合は実行ファイル隣の `assets/local/` を使います。
ソースは `assets/Source/PreviewRoom/`、変換済みデータは `assets/PreviewRoom.ptscene` です。
カウンター天板は鏡、冷蔵庫は粗さのある青い金属に設定しています。

## Kitchen Setを表示する

1. [公式ダウンロード一覧](https://openusd.org/release/dl_downloads.html) のKitchen Setを開き、利用者自身で条件を確認してダウンロードします。
2. ZIPを `assets/Source/Kitchen_set/` へ展開します。インストール操作は不要です。参照先のUSDやテクスチャを含むフォルダ構造を維持してください。
3. [UsdConverter](../../tools/UsdConverter/README.md) を別ソリューションでビルドします。
4. 実際に展開された `Kitchen_set.usd` のパスを指定し、変換します。

```powershell
.\build\x64\Release\UsdConverter\UsdConverter.exe assets/Source/Kitchen_set/Kitchen_set.usd assets/local/Kitchen_set.ptscene
.\build\x64\Release\UsdKitchen\UsdKitchen.exe
```

相対パスはカレントディレクトリ基準です。空白を含むパスは引用符で囲んでください。
Kitchen Setは同梱・自動取得しません。公式の利用条件に従い、入手データおよび変換したデータもコミットしないでください。
`assets/local/`、`assets/Source/Kitchen_set/`、`build/` はGit管理外です。
利用者が配置した `assets/Source/Kitchen_set/Kitchen_set.usd` と `Kitchen_set_instanced.usd` で、Debug / Releaseの変換・描画を確認しました。
2022-07-21版（同梱VERSION.txt）の通常版とインスタンス版は両構成で同じバイナリになり、1,788メッシュ出現、536,918三角形、25,980 meshlets、1,370,251頂点です。
原本は移動せず、次のコマンドで変換テストの結果・ログ・128サンプルの描画画像を `build/checks/kitchen-<構成>/` に保存できます。通常利用の `assets/local/Kitchen_set.ptscene` は変更しません。

```powershell
.\tests\RunKitchenSet.ps1 -Configuration Debug
.\tests\RunKitchenSet.ps1 -Configuration Release
```

このスクリプトは、メインソリューションと独立したUsdConverterを同じ構成でビルドしてから使用してください。
原本の位置は `-SourceDirectory`、出力先は `-OutputDirectory` で変更できます。保存PNGは開いて表示内容も確認してください。
`--usd assets/Source/Kitchen_set/Kitchen_set_instanced.usd` を指定してアプリから変換・読み込みする経路も、Debug / Releaseで確認済みです。
Subdivisionは細分化せず制御ケージとして表示します。今回のデータは定数PBRマテリアルのバインドがなく、displayColorと既定マテリアルで描画されます。
初期の全体表示には天井も含まれます。室内の確認には既存のフリーカメラを使用できます。

## アプリから変換する

`Scene files` を開き、`Open USD...` でソースを選び、必要なら `Choose converter...` でビルド済み `UsdConverter.exe` を選び、`Convert and load` を押します。
変換は独立プロセスで実行します。既定では同じ構成の隣のUsdConverter、次にRelease構成を探します。
変換中も描画でき、キャンセル可能です。失敗時は現在のモデルを維持し、Conversion logにエラーを表示します。
通常の変換結果は `assets/local/<USDのファイル名>.ptscene` に保存します。同名USDの再変換ではこのファイルを更新します。ログは実行ファイル隣の `cache/` に保存します。
テストや別の保存先を使う場合は `--conversion-output <保存先.ptscene>` で上書きできます。相対パスはカレントディレクトリ基準です。
`Open binary...` から既存バイナリも開けます。

```powershell
.\build\x64\Debug\UsdKitchen\UsdKitchen.exe --usd assets/Source/PreviewRoom/PreviewRoom.usda --converter "build/x64/Release/UsdConverter/UsdConverter.exe"
```

自動撮影では変換・ロードが終わってから撮影します。起動時の変換失敗は終了コード1と標準エラーで通知します。

```powershell
$app = Start-Process .\build\x64\Release\UsdKitchen\UsdKitchen.exe -ArgumentList '--usd "assets/Source/PreviewRoom/PreviewRoom.usda" --conversion-output "build/checks/usd.ptscene" --screenshot "build/checks/usd.png"' -WindowStyle Hidden -Wait -PassThru
$app.ExitCode
```

## 直接照明

`Direct lighting` から1灯の方向光または点光源を選択します。方向光が初期設定です。

- 方向光: 方位角・仰角、色、照射強度を調整します。Headlightを有効にするとカメラ方向に追従します。
- 点光源: メートル単位のワールド位置、色、強度を調整します。光量は距離の二乗に反比例します。モデルを開くと光源位置をモデルの範囲に合わせます。
- Shadows: ヒット点から光源方向へRayQueryで遮蔽を検査します。点光源では光源までの区間だけを検査します。すべての三角形を不透明・両面の遮蔽物として扱うハードシャドウです。
- Shadow bias: 幾何法線方向のオフセットの最小値（m）。座標に比例するオフセットも加味し、自己交差を抑えます。非常に小さい形状では調整が必要です。
- Ambient fill: 影の部分の確認用に加える一定の補助光です。0で無効にできます。間接照明やGIではありません。

シェーディングは線形RGBのGGX鏡面反射と、Fresnel・Metallicで抑制したLambert拡散反射を組み合わせます。光の強度は方向光では相対的な照射強度、点光源では距離減衰前の相対強度として扱います。
蓄積した線形HDR放射輝度にExposureを適用し、ReinhardトーンマッピングとsRGB変換を行います。
USDのライトは読み込まず、このサンプル内で設定した光源を使用します。矩形面光源は下記の別パネルで追加できます。

確認用の起動オプション:

```powershell
.\build\x64\Debug\UsdKitchen\UsdKitchen.exe --light point --screenshot build/checks/point.png
.\build\x64\Debug\UsdKitchen\UsdKitchen.exe --light directional --no-shadows --screenshot build/checks/no-shadows.png
.\build\x64\Debug\UsdKitchen\UsdKitchen.exe --ambient 0 --light-strength 0 --ibl-intensity 0 --screenshot build/checks/no-light.png
```

`--light-strength` は0～200、`--ambient` は0～2です。未指定の強度は方向光4、点光源40、補助光0です。
実装はDXR Tier 1.1 / SM6.5の既存要件内で、各ヒット点からインラインRayQueryを実行します。
追加のTraceRay再帰やシャドウ用のシェーダーテーブルは必要ありません。

## 矩形面光源とソフトシャドウ

`Rectangle lights` の `Enable rectangle` で片面発光の矩形ライトを有効にします。最大32灯で、既定は無効な1スロットです。
`Add rectangle` / `Duplicate rectangle` / `Remove rectangle` で追加・複製・削除し、`Rectangle index` で編集対象を選びます。最後の1スロットは削除せず無効化できます。NEE / MISの切り替えは全矩形に共通です。
位置、幅・高さ（0.01～100 m）、方位角・仰角、線形RGB色、放射輝度（0～10000）を調整できます。
仰角-90度は下向き、+90度は上向き、仰角0度・方位角0度は+Z向きです。ロールはありません。
値はワールド座標で、モデルの再読み込みでは自動配置しません。変更時は蓄積がリセットされます。

面積×線形輝度のCDFから矩形1灯を選び、面積上で一様に点を選んで立体角PDFへ変換します。光源選択確率をNEE・BSDFヒット・ReSTIRのPDFへ共通で含めます。光源までの区間をRayQueryで検査するため、面積に応じた半影が現れます。
`Rectangle NEE / MIS` は既定で有効です。無効時はBRDFサンプリングだけになり、特に小さいライトでは収束が遅くなります。
光源に到達したBRDFレイとのMISはpower heuristicを使い、一次レイと完全鏡のdelta反射には重みを掛けません。
矩形はカメラ・反射レイに対しても見え、裏面は黒い不透明面です。IBLと既存ライトの遮蔽にも含みます。
矩形の影は常に有効で、Direct lightingのShadows切り替えとは独立です。

放射輝度は面積あたりの発光で、サイズを大きくすると総放射束も増えます。同じ総放射束で影を比較する場合は、面積に反比例して放射輝度を変えます。
光源色×放射輝度を線形HDRとして使う相対的な照明設定です。

```powershell
.\build\x64\Release\UsdKitchen\UsdKitchen.exe --rect-light --rect-position -0.4 2.2 0.2 --light-strength 0 --ibl-intensity 0 --samples 256
```

CLIの設定・自作比較モデル・検証手順は [矩形ライトのテスト](../../tests/RectangleLight.md) を参照してください。

## PBRマテリアルと鏡面反射

ゲームで一般的なMetallic / Roughness方式です。`PBR material` からマテリアル番号を選び、以下の定数パラメータを編集できます。
変更は同じマテリアルを参照する全三角形へ反映され、蓄積がリセットされます。編集内容は実行中のみ有効です。

| パラメータ | 内容 |
| --- | --- |
| Base Color | 線形RGB、0～1。頂点色に乗算。非金属では拡散色、金属では鏡面反射のF0色 |
| Metallic | 0～1。0は非金属、1は金属。金属の拡散成分は0 |
| Roughness | 知覚的な粗さ、0～1。0は完全鏡、値を上げると反射・ハイライトが広がる |
| Specular | 非金属のF0を `0.08 × Specular` で指定。既定0.5は4%、金属ではBase Colorを使用 |
| Ambient Occlusion | 0～1。補助光と反射後の経路に掛けるアーティスト用係数。直接光・発光には掛けない |
| Emissive Color / Strength | 線形RGBの色×強度。可視の発光と、反射レイが当たった際の照明に使用 |

USDのメッシュ／GeomSubsetにバインドされた定数マテリアルを取り込みます。Preview Roomの番号0は未バインドの共通マテリアル、1は鏡、2は青い金属です。
`Reset material` は選択したマテリアルを既定の白い非金属に戻します。元のUSD値に戻す場合はモデルを開き直してください。
Normalは既存の頂点法線を使用します。UVテクスチャ、Normal Map、Opacity／透過、Clear Coat、異方性はまだ扱いません。

鏡面項はGGX / Trowbridge-Reitz NDF、Smith-GGXのheight-correlated visibility、Schlick Fresnelです。
Roughnessは二乗してGGXのalphaへ変換し、非ゼロの粗さは数値安定性のため最低0.02を使います。0.001未満は完全鏡のdelta反射です。
直接光にも同じBRDFを使用します。完全鏡は有限幅の直接光ローブを持たず、次のレイが映したIBL・物体・発光面を反射します。
拡散とGGXの混合サンプリングで経路を選び、`BRDF × cos / 混合PDF` で重みを更新します。完全鏡はFresnelとローブ選択確率で重みを更新します。
GGXは等方性VNDF（視線から見える微小面法線の分布）を [Heitz 2018](https://jcgt.org/published/0007/04/01/paper.pdf) の方法でサンプリングします。
鏡面の反射方向PDFは `D(H) G1(V) / (4 N·V)`、拡散との混合PDFはIBL・矩形光源・発光メッシュのNEE / MISでも共用します。完全鏡のdelta反射は従来どおり扱います。
GGXで面の下に出たサンプルは捨て、再抽選しません。
多重散乱はKulla–Conty方式の追加ローブで近似補償します。単一散乱GGXのエネルギー損失を積分したLUTを使い、粗い金属が暗くなる現象を抑えます。
補償成分は拡散と同じ余弦半球からサンプリングし、VNDFとの混合確率を含めたPDFを全光源のMISで共用します。完全鏡のdelta成分には補償しません。
ImGuiの `GGX multiple scattering` または `--no-multiscattering` で補償なしと比較できます。UIの切り替えでは蓄積をリセットします。
LUTは生成済みデータを同梱し、起動時の積分や追加ライブラリは不要です。[生成方法・式と制限](../../tools/GgxEnergyLut/README.md) を参照してください。
拡散項には視線側と入射光側のFresnel透過率を掛け、斜め視線で鏡面項と拡散項の合計が過剰になることを抑えます。
白色環境でのエネルギー増加とサンプリングの整合性は [ホワイトファーナステスト](../../tests/WhiteFurnace.md) で検証できます。

```powershell
.\build\x64\Release\UsdKitchen\UsdKitchen.exe --metallic 1 --roughness 0 --bounces 8 --samples 256 --screenshot build/checks/mirror.png
.\build\x64\Release\UsdKitchen\UsdKitchen.exe --metallic 1 --roughness 0.7 --samples 256 --screenshot build/checks/rough-metal.png
```

`--metallic`、`--roughness`、`--specular` は0～1で、ロード時に全マテリアルへ適用する比較用の上書きです。
Emissive Color（各0～1）×Strength（0～100000）は両面に発光します。
発光三角形の選択確率を面積×線形輝度から構築し、各反射で1点をNEE評価します。選択確率を含む立体角PDFで、発光面へ到達したBRDFレイとMISを行います。
一次レイと完全鏡のdelta反射にはMISの減衰を掛けません。発光面の通常のBRDF反射は維持し、発光成分だけをMISで重み付けします。
光源への遮蔽を常に検査し、解析的な矩形ライトも遮蔽物に含めます。反射上限でのNEEは打ち切り、そこまでに到達した発光面は表示します。
マテリアル編集で発光色・強度が変わると選択分布とGPUの光源表を更新し、蓄積をリセットします。非発光面を新しく光らせることや、全消灯にも対応します。
Metallic / Roughnessなどの変更だけでは光源表を再構築しません。ゼロ面積の三角形は光源サンプリングから除外します。
光源表は表示時にmeshletから派生生成するため、USD SDKや独自バイナリの形式変更は不要です。
自作の複数光源モデルと画像・数値検証は [発光メッシュのテスト](../../tests/EmissiveLights.md) を参照してください。
参考: [Filamentの標準PBRモデル](https://google.github.io/filament/Filament.md.html)、[UsdPreviewSurface仕様](https://openusd.org/dev/spec_usdpreviewsurface.html)

## 間接照明とIBL

一次ヒットの発光メッシュ照明・IBL・矩形光源には、オプションでReSTIR DIを使用できます。発光三角形0でもIBLだけ、矩形だけで動作します。既定は従来のNEE / MISです。
ImGuiの `ReSTIR DI`、または `--restir off|ris|temporal|spatial` で、8候補RIS・時間再利用・空間再利用を段階的に比較できます。
G-bufferの法線・深度・モーション・Reservoir履歴は `--restir spatial --restir-debug normal|depth|motion|reservoir` で表示します。
G-buffer viewはNEE / MIS選択中や発光三角形0でも使用できます。例えば `--restir-debug normal` だけで法線を表示します。
二次ヒット以降は通常のNEE / MISです。`--no-ibl-mis` / `--no-emissive-mis` / `--no-rect-mis` は対応する光源をReSTIRの候補からも除きます。矩形は最大32灯で、直接見える光源と完全鏡への映り込みを維持します。
`--ibl-rotation <度>` で環境を回転し、`--gpu-timing <出力.csv>` でGPUの照明パス時間を保存できます。[仕組み・検証・メモリ使用量と制限](../../tests/RestirDI.md) を参照してください。

Russian Rouletteは既定で有効です。3回の反射後からスループットのRGB最大成分に応じて継続を抽選し、生存した経路を確率の逆数で補正します。
最大反射回数は従来どおり上限として残ります。ImGuiの `Russian roulette (after 3 bounces)` または `--no-russian-roulette` で無効時と比較できます。
同じサンプル数ではノイズが増える場合があります。[実装と数値・画像検証](../../tests/RussianRoulette.md) を参照してください。

初期設定で最大3回の反射を追跡します。各ヒット点で上記の直接照明・発光を評価し、PBRマテリアルに応じて拡散・鏡面の次レイを生成します。
一次レイ・反射後のレイともMiss ShaderでIBLテクスチャを参照します。IBLは背景にも、物体の照明にも使用します。
光源までの遮蔽レイのMissは可視判定だけに使い、IBLの寄与を二重に加算しません。

`Indirect lighting / IBL` の操作:

- Path bounces: 拡散・鏡面を合わせた反射回数（0～8、既定3）。0では物体は直接照明・発光のみとなり、背景のIBL表示は維持します。
- Samples per frame: 1フレームあたりのサンプル数（1～16、既定1）。カメラレイの位置もジッターします。
- IBL intensity / rotation: 環境マップの強度（0～100、既定1）と水平方向の回転。
- IBL NEE / MIS: 既定で有効。IBLの重点サンプリングと遮蔽レイによる直接評価を、BRDFサンプリングとMISで統合します。切り替えると蓄積をリセットします。
- Emissive NEE / MIS: 既定で有効。発光メッシュのNEE/MISを切り替えます。Emissive trianglesは有効な発光三角形数です。
- Open IBL (.hdr): 緯度経度形式のRadiance HDRを選択します。Preview skyで自作の空マップに戻せます。
- Reset accumulation: 蓄積をやり直します。カメラ、光源、IBL、露出、バウンス数、サンプル数、描画領域、モデルが変わった場合も自動でリセットします。

既定のIBLはコードで生成した512×256の自作HDR空マップです。外部データや追加ソフトウェアの取得は不要です。
外部 `.hdr` はRGBE、modern scanline RLEまたは非圧縮、Y-majorの `-Y/+Y` と `+X/-X` に対応します。
画像は線形RGBとして読み込み、右手系Y-upのレイ方向から緯度経度UVを計算します。横方向はラップ、縦方向はクランプします。
最大16384画素／軸、合計16777216画素です。旧式のrepeat符号化、XYZE、EXR、キューブマップは未対応です。
FORMAT・解像度以外のHDRメタデータは適用しません。入手したHDRは `assets/local/` などGit管理外の場所に置いてください。

GPU上のRGBA32Fテクスチャに線形HDRの平均を蓄積し、表示時にトーンマッピングします。動かしている間は蓄積がリセットされるため、静止するとノイズが減ります。
IBLロード時に線形輝度と画素の立体角から行・列のCDFを作り、GPUのバッファへ配置します。
95%の輝度分布に5%の一様球分布を混ぜ、補間で明るくなる隣接画素にも確率を与えます。全黒マップは一様球分布へフォールバックします。
選んだ画素の内部では方位角とcosθを一様にサンプリングし、PDFを立体角あたりで評価します。CDFのfloat丸めを含め、逆変換とPDFの確率を一致させます。
各反射でIBLを1サンプル評価し、既存のBRDFサンプルとpower heuristic（指数2）で重み付けします。
反射レイがIBLへMissした場合だけMISの重みを掛け、背景・完全鏡のdelta反射・物体の発光にはその重みを掛けません。
NEEにもAOを適用し、反射回数上限ではNEEを追加しません。IBLの遮蔽は常に検査し、Direct lightingのShadows切り替えとは独立です。
固定バウンス上限とRussian Rouletteによるパストレーシングです。DLSS Ray Reconstructionは任意で使用できます。透過反射は未実装です。

### 拡散GIの候補生成

`ReSTIR GI` または `--restir-gi candidates` で、既存PTから拡散間接光の候補を生成し、再利用なしで評価します。
`--gi-view diffuse|remainder|radiance|normal|pdf|valid|error` で成分・候補を確認できます。通常表示は `--gi-view output` です。
`--restir-gi temporal` は平坦なLambert二次面の候補を前フレームから1つ再利用します。対象外の鏡面などは通常PTで評価します。
`--restir-gi spatial` は時間再利用に加え、同じフレームの近隣4画素から候補を再利用します。生候補を準備するため、描画の前に1画素あたり1経路のプリパスを追加します。
`--no-gi-spatial` と `--no-gi-history` で各再利用を個別に無効化でき、`--gi-view history|spatial|diffuse-noisy` で候補の受理・選択や非蓄積GIを確認できます。

ReSTIR PTの準備として、`--path-replay` は通常PTの各サンプルを開始時の乱数シードから再現し、照合に失敗した画素をマゼンタで表示します。`--path-replay-check` では、一致を緑、2本以上のレイを追跡した一致をシアンで表示します。両モードではReSTIR DI/GIとRay Reconstructionを無効にします。検証手順は [PathReplay.md](../../tests/PathReplay.md) を参照してください。

`--restir-pt temporal` または `PT temporal (full primary BSDF)` で、前フレームの生候補1本への時間再接続を試せます。粗さ0.2以上の一次面ではGGXを含むBSDF全体を評価し、平坦なLambert二次面からの反射放射輝度を再利用します。`--gi-view history` で履歴を表示し、`--no-gi-history` で現在の候補だけと比較できます。対象経路と検証手順は [RestirGI.md](../../tests/RestirGI.md) を参照してください。

`--restir-pt hybrid` または `PT hybrid (one glossy bounce)` では、鏡面・低粗さの一次反射を乱数から1回再生し、粗い二次面から保存済みの平坦なLambert頂点へ再接続します。履歴表示では再接続成功が緑、履歴候補の選択がシアン、一次面は一致したものの再生・接続に使える候補がなかった場合が紫です。検証手順は [PtHybrid.md](../../tests/PtHybrid.md) を参照してください。

`--gi-view hybrid-cost` は再生レイ数・可視性チェック数・履歴採用数をRGBで表示します。`--hybrid-unoptimized` で黒い候補への再生も有効にし、通常のhybridと比較できます。
再帰的な履歴蓄積と空間再利用は未実装です。詳しい分離方法、対象範囲、メモリ量、検証は [ReSTIR GIの手順](../../tests/RestirGI.md) を参照してください。
小さく明るい光を含むHDR、光沢面のコースティクス、閉じた空間では多くのサンプルが必要です。反射上限で打ち切った経路の先は黒くなります。

```powershell
.\build\x64\Release\UsdKitchen\UsdKitchen.exe --ibl "assets/local/studio.hdr" --spp 4 --samples 256 --screenshot build/checks/ibl.png
.\build\x64\Release\UsdKitchen\UsdKitchen.exe --light-strength 0 --samples 256 --screenshot build/checks/ibl-only.png
.\build\x64\Release\UsdKitchen\UsdKitchen.exe --ibl-intensity 0 --bounces 3 --samples 256 --screenshot build/checks/direct-bounces.png
.\build\x64\Release\UsdKitchen\UsdKitchen.exe --ibl "assets/local/studio.hdr" --no-ibl-mis --spp 4 --samples 256 --screenshot build/checks/ibl-bsdf-only.png
```

`--samples` は自動撮影前に待つ最小蓄積サンプル数（1～65536）です。`--spp` の指定や撮影フレームの実行により指定数以上になる場合があります。
省略時の自動撮影は従来どおり数フレームの簡易確認です。間接照明の確認には `--samples 128` 以上を指定してください。
HDR読み込みのCPUテストはDebug / Releaseの `EnvironmentMapTests.exe build/checks` で実行できます。
同じテストでCDFの正規化・立体角PDF・黒マップ・高輝度画素の選択確率も検査します。
`--no-ibl-mis` は従来のBRDFサンプリングだけに戻す比較用オプションです。NEE / MISのエネルギー検証は [WhiteFurnace](../../tests/WhiteFurnace.md) に含みます。
`--no-emissive-mis` は発光メッシュのNEE/MISを無効にしてBRDFサンプリングだけで比較します。IBL・矩形ライトのMISとは独立です。

参考: [PBRT: Infinite Area Lights](https://www.pbr-book.org/4ed/Light_Sources/Infinite_Area_Lights)
、[PBRT: A Better Path Tracer](https://pbr-book.org/4ed/Light_Transport_I_Surface_Reflection/A_Better_Path_Tracer)

## 対応範囲

静的メッシュの形状確認用です。階層変換、参照・ペイロード、ネイティブインスタンス、凹多角形、法線、displayColor、定数のマテリアル色を変換します。
ステージ開始時刻をメートル単位、右手系Y-upの三角形に固定します。
サブディビジョンは制御ポリゴンのままです。テクスチャ、USDライト、透明度、シェーダーネットワーク、アニメーション、PointInstancerは未対応です。
定数PBRパラメータによるプレビューであり、USDの最終ルックを再現するものではありません。
CLASはまだ使用していません。
