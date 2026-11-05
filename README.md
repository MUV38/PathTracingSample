# PathTracingSample

Visual Studio 2026で開く、Win32 / DirectX 12 / DXRのサンプル集です。
Agility SDK **1.722.0-preview** と Dear ImGui **v1.93.0** を使用します。
PIX用のCPU／GPUマーカーには、公式NuGetの **WinPixEventRuntime 1.0.240308001** を使用します。Debug／Releaseの両方で有効です。
UsdKitchenのビルド前に `.\tools\RestoreStreamline.ps1` でStreamline **2.14.16** を復元してください。対応RTX GPUではDLSS Ray Reconstructionを利用できます。DLLのライセンス確認と配置、`--rr` による実行は [RRの導入手順](tests/RayReconstruction.md) を参照してください。

## サンプル

| プロジェクト | 内容 | 必要なGPU機能 |
| --- | --- | --- |
| PathTracingSample | 画面クリア、ImGui、スクリーンショット | DirectX 12。ハードウェアが利用できなければWARP |
| DxrCube | 12枚の三角形のキューブをDXRで描画 | DXR Tier 1.0以上に対応するGPUとドライバ |
| UsdKitchen | meshletのPBRパストレーシング、拡散・鏡面反射、直接・間接照明、IBL、アプリ内変換 | DXR Tier 1.1以上 |
| SampleFramework | 上記で共有する静的ライブラリ | 単独では起動しません |
| CameraTests | カメラの移動・注視点操作・速度変更のテスト | GPU不要のコンソールテスト |
| SceneFileTests | バイナリの往復・破損検出、USD変換結果のテスト | GPU / USD SDK不要 |
| EnvironmentMapTests | Radiance HDRの復号・向き・不正入力のテスト | GPU / USD SDK不要 |
| MeshLightTests | 発光三角形の選択分布、編集時の更新、自作描画フィクスチャの生成 | GPU / USD SDK不要 |
| WhiteFurnaceTests | 実描画と共通のBRDFを白色環境で積分し、エネルギー増加・不正値を検出 | DirectX 12 / SM6.0、WARP可 |

DXRキューブは1個のBLASとTLAS、レイトレーシングPSO、シェーダーテーブルを構築し、
DispatchRays()からRay Generation / Miss / Closest Hitシェーダーを実行します。
Closest Hitで面の法線に応じた拡散照明を計算します。カメラの方位角・仰角・距離と色をImGuiから変更できます。
リサイズ時はDXR出力テクスチャを再作成し、DXR描画後にImGuiを重ねます。
DxrCubeは単発の一次レイによるDXR入門サンプルです。多重反射・蓄積、影、CLASは使用していません。

UsdKitchenはmeshletを読み込み、Metallic / Roughness方式のPBR、GGX鏡面反射、ハードシャドウ、複数バウンスの間接照明を描画します。
拡散GIの初期候補生成と、平坦なLambert二次面を対象にした1フレームの時間再利用・近隣4画素の空間再利用にも対応します。
再帰的な履歴蓄積・空間再利用は未実装です。表示と検証は [ReSTIR GIの手順](tests/RestirGI.md) を参照してください。
矩形面光源1灯によるソフトシャドウと、光源の表示・鏡への映り込みにも対応します。`Rectangle light` パネルから有効にします。
PBRマテリアルのEmissiveを持つ複数メッシュも、面積と輝度による重点サンプリング、NEE/MISで照明として評価します。
比較モデルと検証手順は [発光メッシュの説明](tests/EmissiveLights.md) にあります。
Miss時はIBLを参照します。自作の空マップを既定で使い、Radiance HDRの読み込みにも対応しています。
IBLは輝度に応じた重点サンプリングとNEEを行い、BRDFサンプリングとMISで統合します。
ImGuiの `IBL NEE / MIS` または比較用の `--no-ibl-mis` で従来方式へ切り替えられます。
ImGuiで光源、IBL、バウンス数、サンプル数を調整でき、静止中はHDRのサンプルを蓄積します。
詳しくは [UsdKitchenの照明設定](samples/UsdKitchen/README.md#間接照明とibl) を参照してください。
ホワイトファーナステストはビルド後に `tests/RunWhiteFurnace.ps1 -Configuration Debug`（またはRelease）で実行します。
BRDFの数値検証に加え、UsdKitchenで白色環境の画像を撮影・判定します。画像検証にはDXR Tier 1.1が必要です。
判定内容と出力ファイルは [テスト手順](tests/WhiteFurnace.md) を参照してください。

## ビルドと実行

CLASを自分で実装するための検証プロジェクトは [ClasSandbox](samples/ClasSandbox/README.md) です。
meshletのGPU入力と通常DXRの比較描画を用意し、CLAS処理は未実装箇所として分離しています。

[Cluster Templates](samples/ClusterTemplatesSandbox/README.md) と [PTLAS](samples/PtlasSandbox/README.md) も別々の実行プロジェクトで試せます。Cluster Templatesは波状パッチ群、PTLASは通常BLASを共有する箱のグループから始められます。

必要な環境:

- Agility SDK対応のWindows 11、または更新済みWindows 10
- Visual Studio 2026の「C++によるデスクトップ開発」とMSVC v145
- DXCを含む新しいWindows SDK（10.0.26100.0でビルド確認）
- 初回のNuGetパッケージ復元時に接続できるネットワーク環境

1. ルートの `PathTracingSample.slnx` をVisual Studio 2026で開きます。
2. 初回はソリューションを右クリックして「NuGetパッケージの復元」を実行します。
3. `Debug | x64` または `Release | x64` を選んでビルドします。
4. 起動したいサンプルを右クリックし、スタートアッププロジェクトに設定します（既定はUsdKitchen）。
5. F5でデバッグ実行、Ctrl+F5でデバッグなしで実行します。

Developer PowerShell for VS 2026からは次のコマンドも使用できます。

```powershell
msbuild PathTracingSample.slnx /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64
.\build\x64\Debug\DxrCube\DxrCube.exe
.\build\x64\Debug\PathTracingSample\PathTracingSample.exe
```

出力先は `build/x64/<構成>/<プロジェクト名>/` です。中間ファイルもプロジェクト別に分離しています。
Cube.hlslはWindows SDKのDXCでlib_6_3としてビルドし、実行ファイル隣のCube.dxilに出力します。
実行時のカレントディレクトリに依存せず、実行ファイルの位置からシェーダーを読み込みます。
配布時はCube.dxilと下記のD3D12フォルダも含めてください。

packages.configの公式Agility SDKとWinPixEventRuntimeをルートのpackagesフォルダに復元します。
共通MSBuild props / targetsがインクルードパスを設定し、各実行ファイル隣のD3D12フォルダにランタイムDLLとPDBを配置します。
各実行ファイルはD3D12SDKVersion = 722と、相対パスD3D12を示すD3D12SDKPathをエクスポートします。
Agility SDKは将来のCLAS利用を見据えた指定版を維持しています。DXR対応の表示はCLAS対応を意味しません。

## USDモデルとKitchen Set

`UsdKitchen` は `.ptscene` 独自バイナリだけを読み込み、OpenUSDにはリンクしません。
初期表示はGit管理外の `assets/local/Kitchen_set.ptscene` があればKitchen Setを優先し、なければ同梱の自作 **Preview Room**（192三角形）を表示します。Pixar Kitchen Set自体は同梱しません。
利用者が `assets/Source/Kitchen_set/` に配置した実データは、通常版・インスタンス版ともDebug / Releaseで変換・描画を確認済みです。
このフォルダもGit管理外です。再確認には `tests/RunKitchenSet.ps1 -Configuration Debug` またはReleaseを使用できます。
変換ツールはmeshoptimizer **v1.3** で最大64頂点・126三角形のmeshletへ分割します。
表示アプリはPBRマテリアルを含むmeshlet専用v3バイナリを読みます。旧v1 / v2は未対応のためUSDから再変換してください。
USDを変換する別ソリューションは `tools/UsdConverter/UsdConverter.slnx` です。
既存OpenUSD SDKの指定、変換と表示、Kitchen Setの入手後の手順は
[USDサンプルの説明](samples/UsdKitchen/README.md) と [変換ツールの説明](tools/UsdConverter/README.md) を参照してください。

## 構成とサンプル追加

- `common/`: ウィンドウ、DirectX 12初期化、ImGui、GPU同期、撮影を共有するSampleFramework
- `samples/Clear/`: 既存の画面クリアサンプルのエントリーポイント
- `samples/DxrCube/`: DXRキューブのC++とHLSL、独立した実行プロジェクト
- `samples/UsdKitchen/`: 変換済みシーンのDXR表示、外部変換プロセスの呼び出し
- `tools/UsdConverter/`: OpenUSD SDKを使用する独立した変換ソリューション
- `assets/`: 自作プレビューのUSDソースと変換済みバイナリ。入手したモデルはGit管理外の `assets/local/` へ配置
- `build_support/`: 全プロジェクトの共通Visual Studio設定とAgility SDK設定
- `third_party/imgui/`: ライセンス付きの公式ソース。元の書式を維持

サンプルはcommon/Sample.hの初期化・更新・UI・描画コールバックを必要な分だけ設定します。
大きな継承階層や共通Renderer内のサンプル別分岐は使いません。
新しいサンプルを増やす手順と最小プロジェクトの例は [samples/README.md](samples/README.md) にあります。

## 共通の操作と動作

- 2枚のバックバッファ、VSync付きPresent、フェンスによる同期を使用します。
- 実装を簡潔にするため、毎フレームGPUの完了を待ちます。
- リサイズに対応し、最小化中は描画を休止します。
- 共通パネルでFPS、フレーム時間、バックバッファサイズ、DXR対応状況を表示します。
- Clear colorで背景色、Reset clear colorで初期色に戻せます。
- Show ImGui demoで公式デモを表示できます。
- Escまたは閉じるボタンで終了します。ImGuiがキーボードを使用している間はEsc終了を抑制します。
- Debugでは利用可能ならD3D12デバッグレイヤーを有効にします。
- DXRサンプルは対応GPUを優先して選び、存在しない場合は明示的にエラー終了します。
- Dear ImGuiは共通ライブラリで一度だけコンパイルします。専用SRVヒープと割り当て／解放コールバックを使用します。

## DXRサンプルのカメラ操作

マウス操作は右側の描画領域から開始します。ImGui上のクリックやホイールはUI操作として扱います。

| 操作 | 動作 |
| --- | --- |
| 右ボタンを押しながらマウス移動 | フリールック |
| 右ボタン＋W / S / A / D | 前進 / 後退 / 左 / 右 |
| 右ボタン＋Q / E | ワールドの下 / 上へ移動 |
| 上記の移動中にShift | 一時的に4倍速 |
| ホイール上 / 下 | 基本移動速度を増加 / 減少（0.05～100 units/s） |
| Alt＋左ドラッグ | 現在の注視点を中心に回転 |
| Alt＋中ドラッグ | カメラと注視点を一緒にパン |
| Alt＋右ドラッグ、Alt＋ホイール | 注視点までの距離を変更するドリー |
| F、またはFocus originボタン | キューブの中心を注視点にする |
| Reset camera | 位置、注視点、角度、距離、移動速度を初期化 |

ホイールだけでは位置を動かさず、Move speedに表示する基本速度を変更します。
右ボタンを押して移動している間もホイールで速度を調整できます。Alt＋ホイールのドリーとは分離しています。
Camera detailsで現在位置と注視点を確認できます。
フリールックではカメラ位置を固定して視線を変え、その先に注視点を更新します。
パンやフリー移動後のAlt回転は、移動後の注視点を使用します。

相対マウス移動はWin32 Raw Inputから取得し、画面端でも回転を継続できます。
操作終了時はカーソルを開始位置に戻し、フォーカスを失ったら操作を解除します。
Raw Inputを使用できない環境では通常のマウス差分にフォールバックします。

共通カメラは `common/Camera.h` の入力アクションを受け取る独立したクラスです。
ImGuiやDXRには依存せず、別のサンプルからも再利用できます。

```powershell
.\build\x64\Debug\CameraTests\CameraTests.exe
.\build\x64\Release\CameraTests\CameraTests.exe
```

カメラテストは移動量、斜め移動、フレーム時間、Shift加速、ホイール速度の増減と上下限、フリールック、注視点回転、パン、ドリー、リセットを確認します。

## スクリーンショット

すべてのサンプルでSave screenshot (F12)ボタンまたはF12キーを使用できます。
実行ファイル隣のscreenshotsフォルダに日時付きPNGを保存し、Open screenshot folderでそのフォルダを開きます。
撮影前でフォルダが存在しない場合は作成します。保存・フォルダを開く処理の失敗はパネルに表示し、描画を継続します。
画像にはImGuiも含み、ウィンドウ枠や他のアプリは含みません。外部の画面取得ツールは不要です。
GPU読み戻し後、Windows標準のWICでPNGを保存します。

自動確認では3フレーム描画後に指定先へ撮影して終了します。
UsdKitchenで蓄積後に確認する場合は `--samples 128` などを併用すると、指定数以上のサンプルが蓄積するまで待ってから撮影します。
相対パスはカレントディレクトリ基準で、必要な親フォルダを作成し、既存ファイルは上書きします。

`--screenshot-count 16` を併用すると、撮影開始後の連続した16描画フレームを `rr-0000.png` ～ `rr-0015.png` のように保存して終了します。指定範囲は1～256で、1枚の場合は従来どおり指定したファイル名を使います。PNG保存中は描画が待つため、実時間のFPS計測用には使わないでください。

```powershell
.\build\x64\Release\UsdKitchen\UsdKitchen.exe --rr --samples 128 --spp 1 --screenshot-count 16 --screenshot build/checks/rr.png
```

```powershell
$app = Start-Process -FilePath .\build\x64\Release\DxrCube\DxrCube.exe -ArgumentList '--screenshot "build/checks/dxr-cube.png"' -PassThru -Wait
$app.ExitCode
```

成功時は終了コード0、失敗時は1です。エラー内容は標準エラーとデバッガ出力にも書き込みます。
自動確認時はエラーダイアログを表示しません。保存したPNGも開いて、描画内容を確認してください。

## PIXキャプチャ

NuGet復元でWinPixEventRuntimeを取得し、ビルド時にDLLとライセンスを実行ファイル隣へコピーします。PIX本体は別途必要で、自動インストールしません。

PIXがインストールされていれば、通常起動でもキャプチャ層をDirectX／Streamlineの初期化前に読み込みます。更新前から実行中のアプリは一度終了して起動し直してください。ImGui上部の `PIX GPU capture` で状態を確認し、`Capture next frame (.wpix)` から保存できます。既定で保存後にPIXを開きます。`Open in PIX after saving` で切り替えられます。

```powershell
.\build\x64\Release\UsdKitchen\UsdKitchen.exe --pix
.\build\x64\Release\UsdKitchen\UsdKitchen.exe --rr --restir spatial --samples 128 --pix-capture build/checks/frame.wpix --screenshot build/checks/frame.png
```

UIからの保存先は実行ファイル隣の `captures/` です。`Open PIX capture folder` で開けます。PIXが見つからない場合でも描画は継続し、UIに理由を表示します。`--pix` は読み込みを必須にする指定で、`--no-pix` はアプリ側の自動読み込みを無効にします。キャプチャ層の負荷を含めない性能計測には `--no-pix` を使用してください。

`--pix-capture` は撮影準備ができた1フレームを `.wpix` に保存して終了します。`--samples` の待機条件にも従い、単写PNGと同時保存できます。連射撮影との併用はできません。自動テスト用のCLIキャプチャではPIXのGUIを開きません。

フレーム、ReSTIR、パストレーシング、RR、トーンマッピング、画面コピー、ImGui、Present／VSync、GPU待機にマーカーを付けています。詳細と自動検証は [PIXの確認手順](tests/PixCapture.md) を参照してください。

## 参考

- [Agility SDK公式ダウンロード](https://devblogs.microsoft.com/directx/directx12agility/)
- [DXR機能仕様](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html)
- [Microsoft DXR Hello World](https://github.com/microsoft/DirectX-Graphics-Samples/tree/master/Samples/Desktop/D3D12Raytracing/src/D3D12RaytracingHelloWorld)
- [Dear ImGui v1.93.0](https://github.com/ocornut/imgui/tree/v1.93.0)
