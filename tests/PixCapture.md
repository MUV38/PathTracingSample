# PIXキャプチャ

WinPixEventRuntime **1.0.240308001** をルートの `packages.config` で管理します。Visual Studio 2026のNuGetパッケージ復元、または次のコマンドで復元します。

```powershell
msbuild PathTracingSample.slnx /t:Restore /p:RestorePackagesConfig=true
```

共通設定は `build_support/Cpp.props` / `Cpp.targets` に集約し、Debug／Releaseともに `USE_PIX` を定義します。最適化されたReleaseでもマーカーを残します。ビルド時にWinPixEventRuntime.dllとMITライセンス・ThirdPartyNoticesを出力フォルダへコピーします。パッケージ本体とDLLはコミットしません。

PIX本体とGPUキャプチャ層はこのNuGetとは別です。このプロジェクトはPIXをインストールしません。[MicrosoftのWinPixEventRuntimeの説明](https://devblogs.microsoft.com/pix/winpixeventruntime/) と [プログラムからのキャプチャ仕様](https://devblogs.microsoft.com/pix/programmatic-capture/) も参照してください。

## 起動方法

- PIXのLaunchから実行ファイルを起動すれば、そのままGPU Captureを使用できます。
- 通常起動でも、インストール済みPIXの最新キャプチャ層をStreamlineの設定やDirectX API呼び出しより前に読み込みます。見つからない場合は描画を継続し、ImGuiに理由を表示します。明示的な `--pix` は読み込みを必須にする指定で、失敗時は終了コード1を返します。
- アプリ自身で1フレームを保存する場合は `--pix-capture <file.wpix>` を使います。これは `--pix` の読み込みも行います。
- キャプチャ層を自動読み込みしない通常描画は `--no-pix` を指定します。性能の基準値を計測する際に使ってください。`--pix` / `--pix-capture` との併用はエラーです。PIXから起動してすでに注入されているキャプチャ層は取り除きません。

更新前の実行中プロセスに対しては、後からGPUキャプチャ層を読み込んでも接続できません。アプリを再起動して、ImGui上部の `PIX GPU capture` に `Capture layer: loaded / ready to attach` が表示されることを確認してください。

## ImGuiから保存する

`PIX GPU capture` の `Capture next frame (.wpix)` で、ボタンを押した後に描画する1フレームを保存します。保存先は実行ファイル隣の `captures/`、ファイル名は日時・ミリ秒・プロセスIDを含む `.wpix` です。保存後もアプリは動作を続け、再度撮影できます。

`Open in PIX after saving` は既定でオンです。保存に成功すると、Windowsの `.wpix` 関連付けを使ってPIXで開きます。PIXを入れ直すなどして関連付けがない場合は、保存済みファイルを残してUIへ起動エラーを表示します。`Open PIX capture folder` で保存先を開けます。キャプチャ層が未ロードの場合、撮影ボタンを無効にして再起動の案内を表示します。キャプチャに失敗しても描画を継続し、HRESULTを含む理由をUIに表示します。

UIとCLIは共通の要求・開始・終了処理を使用します。CLIは指定先に保存して終了し、PIX GUIは開きません。

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
| CLAS Sandbox | CLAS Sandbox → User CLAS preparation hook（対応環境のみ）→ Trace reference meshlets / Trace user CLAS → Copy to back buffer |
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

スクリプトはPIXの `pixtool.exe` を探します。必要なら `-PixTool <フルパス>` で指定できます。Clear、Cube、ClasSandboxの通常DXR、UsdKitchenのNEE、ReSTIR＋RR、Normalの6ケースをキャプチャし、CSVのイベント一覧に必要なマーカーがあることと、デバッグレイヤー付き再生を検証します。ClasSandboxは `--no-preview` でCLAS未対応環境でも検証できる経路を使用します。NormalではRR評価のマーカーが存在しないことも確認します。`.wpix`、PNG、CSV、解析ログを `build/checks/pix-<構成>/` に保存します。

PNGを開いて表示も確認してください。PIX 2610.28／RTX 3060 TiでDebug／Releaseの計12キャプチャの保存・イベント解析・再生と表示を確認しました。通常起動したアプリのImGuiでの撮影・保存先を開く操作、Kitchen Setの保存後にPIXが自動で開くことも確認しました。Timing Capture、PIX GUIでのプロセス接続操作、他のGPU／PIXバージョンでの動作は未確認です。

この環境ではPIX GUIのStart Analysisで、NVIDIAのハードウェア性能カウンターへの権限不足の警告が出ました。OS／ドライバの設定は変更していません。`.wpix` の保存・イベント一覧・デバッグレイヤー再生の検証と、ハードウェアカウンターの取得は区別してください。GUIで開いているキャプチャの同時CLI解析が失敗したため、UIで保存したKitchen Setは別名コピーを使ってイベント解析・再生を確認しました。
