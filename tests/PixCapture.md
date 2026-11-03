# PIXキャプチャ

WinPixEventRuntime **1.0.240308001** をルートの `packages.config` で管理します。Visual Studio 2026のNuGetパッケージ復元、または次のコマンドで復元します。

```powershell
msbuild PathTracingSample.slnx /t:Restore /p:RestorePackagesConfig=true
```

共通設定は `build_support/Cpp.props` / `Cpp.targets` に集約し、Debug／Releaseともに `USE_PIX` を定義します。最適化されたReleaseでもマーカーを残します。ビルド時にWinPixEventRuntime.dllとMITライセンス・ThirdPartyNoticesを出力フォルダへコピーします。パッケージ本体とDLLはコミットしません。

PIX本体とGPUキャプチャ層はこのNuGetとは別です。このプロジェクトはPIXをインストールしません。[MicrosoftのWinPixEventRuntimeの説明](https://devblogs.microsoft.com/pix/winpixeventruntime/) と [プログラムからのキャプチャ仕様](https://devblogs.microsoft.com/pix/programmatic-capture/) も参照してください。

## 起動方法

- PIXのLaunchから実行ファイルを起動すれば、そのままGPU Captureを使用できます。
- Visual Studioやコマンドラインから起動してPIXを接続する場合は `--pix` を指定します。インストール済みPIXの最新キャプチャ層を、Streamlineの設定やDirectX API呼び出しより前に読み込みます。
- アプリ自身で1フレームを保存する場合は `--pix-capture <file.wpix>` を使います。これは `--pix` の読み込みも行います。

```powershell
.\build\x64\Release\DxrCube\DxrCube.exe --pix-capture build/checks/cube.wpix
.\build\x64\Release\UsdKitchen\UsdKitchen.exe --model assets/PreviewRoom.ptscene --rr --restir spatial --samples 128 --pix-capture build/checks/restir-rr.wpix --screenshot build/checks/restir-rr.png
```

共通フレームワークが3フレーム目以降、かつサンプルの準備条件を満たした時点で開始します。UsdKitchenでは `--samples` に指定した蓄積／描画数を待ちます。キャプチャ前後にGPUの完了を待ち、保存成功後に終了コード0を返します。失敗時は標準エラーを出して1で終了します。相対パスはカレントディレクトリ基準で、親フォルダを作成します。実データや画像を含むキャプチャはGit管理外の `build/checks/` に保存してください。

`--screenshot` との同時保存に対応しますが、`--screenshot-count` が2以上の連射とは併用できません。キャプチャは通常実行より重くなるため、通常時のFPSと混同しないでください。キャプチャ層はGPUオブジェクトの解放まで必要なので、読み込んだプロセスの終了まで保持します。

## マーカー

| 範囲 | マーカー |
| --- | --- |
| CPUフレーム | Frame / CPU、Update / build ImGui、Submit command list、Present / VSync、Wait for GPU fence |
| GPUフレーム | Frame / GPU → Clear back buffer → サンプル描画 → ImGui → Screenshot / GPU readback |
| DXR Cube | DXR Cube → Trace cube → Copy to back buffer |
| UsdKitchen | UsdKitchen → ReSTIR DI / candidates + temporal、ReSTIR DI / spatial reuse、Path tracing、DLSS Ray Reconstruction、Tone mapping / RR view、Copy to back buffer |
| 診断表示 | RR diagnostic guides、Tone mapping / RR view。RR評価を省く表示ではDLSS Ray Reconstructionマーカーを出しません |
| 初期化・読み込み | CPUのDirectX 12／ImGui初期化とシーン読み込み、GPUのBLAS／TLAS構築とIBL転送 |
| PNG保存 | Screenshot / PNG encoding（CPU） |

GPUマーカーはコマンドリストが開いている間に開始・終了し、SDKのRAIIスコープで対応付けます。フレーム全体の内側にパス単位のマーカーを置きます。UsdKitchenの設定（解像度、spp、バウンス数、ReSTIR／RR／View）もポイントマーカーとして記録します。CPUのみのマーカーはTiming Captureで確認できます。今回の自動検証対象はGPU Captureです。

DXRシェーダーは両構成とも `-Zi -Qembed_debug` でデバッグ情報を埋め込みます。シェーダーの最適化は維持します。グラフィックスキューとRRの各ガイド／出力テクスチャにも名前を付けます。

## 自動検証

PIXが入っているPCで、両構成をビルドしてから実行します。RRケースには同意済みのStreamline／DLSS DLLが必要です。

```powershell
.\tests\RunPixCapture.ps1 -Configuration Debug
.\tests\RunPixCapture.ps1 -Configuration Release
```

スクリプトはPIXの `pixtool.exe` を探します。必要なら `-PixTool <フルパス>` で指定できます。Clear、Cube、UsdKitchenのNEE、ReSTIR＋RR、Normalの5ケースをキャプチャし、CSVのイベント一覧に必要なマーカーがあることと、デバッグレイヤー付き再生を検証します。NormalではRR評価のマーカーが存在しないことも確認します。`.wpix`、PNG、CSV、解析ログを `build/checks/pix-<構成>/` に保存します。

PNGを開いて表示も確認してください。PIX 2610.28／RTX 3060 TiでDebug／Releaseの計10キャプチャの保存・イベント解析・再生と表示を確認しました。Timing Capture、PIX GUIでの接続操作、他のGPU／PIXバージョンでの動作は未確認です。
