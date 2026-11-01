# PathTracingSample

DXRによるPath Tracing実装に向けた、Win32 / DirectX 12の最初のステップです。
現段階では1280 × 720のウィンドウを開き、画面クリアとDear ImGuiによる操作パネルを表示します。

## 必要な環境

- Windows 11、またはAgility SDKに対応した更新済みWindows 10
- Visual Studio 2026の「C++によるデスクトップ開発」、MSVC v145、Windows SDK
- 初回のNuGetパッケージ復元時に接続できるネットワーク環境

Agility SDKは **Microsoft.Direct3D.D3D12 1.722.0-preview** に固定しています。
CLAS (Clustered Geometry / Cluster-level Acceleration Structures)を今後利用するための選択で、
今回のコードにはまだアクセラレーション構造やレイトレーシング処理を含めていません。
DXR / CLASの実装には、それぞれの機能に対応したGPUとドライバが必要です。
タイトルバーの `DXR supported` は基本的なDXR対応を示し、CLAS対応の判定ではありません。

## ビルドと実行

1. リポジトリ直下の `PathTracingSample.slnx` をVisual Studio 2026で開きます。
2. 初回はソリューションを右クリックし、「NuGetパッケージの復元」を実行します。
3. `Debug | x64` または `Release | x64` を選択してビルドします。
4. F5でデバッグ実行、またはCtrl+F5でデバッグなしで実行します。

実行ファイルは `build/x64/Debug/PathTracingSample.exe` または
`build/x64/Release/PathTracingSample.exe` に出力されます。
Developer PowerShell for VS 2026からは次のコマンドでも復元・ビルドできます。

```powershell
msbuild PathTracingSample.slnx /restore /p:RestorePackagesConfig=true /p:Configuration=Debug /p:Platform=x64
.\build\x64\Debug\PathTracingSample.exe
```

`packages.config` で指定した公式NuGetパッケージを `packages/` に復元します。
パッケージ付属のMSBuild props / targetsがインクルードパスを設定し、
実行ファイルの隣の `D3D12/` にランタイムDLLと対応するPDBをコピーします。
実行ファイルから `D3D12SDKVersion = 722` と `D3D12SDKPath = ".\\D3D12\\"` をエクスポートし、
アプリケーションローカルのランタイムを選択します。配布時もこのフォルダ構成を維持してください。

## 動作

- ハードウェアGPUを優先してDirectX 12デバイスを作成します。利用できない場合はWARPへフォールバックします。
- 2枚のバックバッファ、RTV、コマンドキュー / アロケータ / リスト、フェンスを作成します。
- `PRESENT → RENDER_TARGET → PRESENT` のリソース遷移とクリア処理を記録し、VSync付きで表示します。
- 初期段階の実装を簡潔にするため、毎フレームGPUの完了を待ちます。
- リサイズ時はGPU完了後にバックバッファとRTVを再作成します。最小化中は描画を休止します。
- Escまたはウィンドウの閉じるボタンで終了します。
- Debugでは利用可能ならD3D12デバッグレイヤーを有効にします。
- 初期化や描画の失敗時には、処理名とHRESULTをダイアログに表示します。
- ImGuiパネルにFPS、フレーム時間、バックバッファのサイズ、DXR対応状況を表示します。
- `Clear color` で背景色を変更し、`Reset clear color` で初期色へ戻せます。
- `Show ImGui demo` で公式デモウィンドウの表示を切り替えられます。
- ImGuiがキーボード入力を使用している間は、Escによるアプリ終了を抑制します。
- `Save screenshot (F12)` ボタンまたはF12キーで、ImGuiを含む描画結果をPNGへ保存します。

`src/Main.cpp` がウィンドウとメッセージループ、`src/Renderer.cpp` がDirectX 12の初期化と描画を担当します。
クリア色はUI、または `Renderer` の `mClearColor` で変更できます。

## Dear ImGui

**Dear ImGui v1.93.0** の公式ソースとMITライセンスを `third_party/imgui/` に同梱しています。
Win32 / DirectX 12バックエンドを使用し、Visual Studioプロジェクトから直接コンパイルします。
ImGui用の追加ダウンロードやパッケージ復元は不要です。外部ソースは元の書式を維持しています。

RendererがImGuiの初期化、フレーム構築、描画、GPU完了後の終了処理を管理します。
フォントなどのテクスチャには、専用のshader-visible SRVヒープ（64スロット）と割り当て／解放コールバックを使用します。
Win32メッセージはImGuiへ転送し、キーボードナビゲーションにも対応しています。
UIのレイアウト設定はファイルに保存しません。

## スクリーンショット

撮影ボタンまたはF12キーを押すと、実行ファイルの隣の `screenshots/` に日時付きのPNGを保存します。
保存結果やエラーはImGuiパネルに表示します。画像サイズは撮影時のバックバッファと同じです。
ImGui描画後、Present前のバックバッファをREADBACKバッファへコピーし、GPU完了後にWindows標準のWICでPNG化します。
ウィンドウ枠や他のアプリは含みません。外部の画面取得ツールや追加ライブラリは不要です。

確認作業向けに、3フレーム描画して指定先へ保存し、そのまま終了するモードもあります。
相対パスはカレントディレクトリを基準にし、保存先のフォルダは必要に応じて作成します。
指定したファイルが存在する場合は上書きします。

```powershell
$app = Start-Process -FilePath .\build\x64\Release\PathTracingSample.exe -ArgumentList '--screenshot "build/checks/screenshot.png"' -PassThru -Wait
$app.ExitCode
```

成功時の終了コードは `0`、失敗時は `1` です。失敗内容は標準エラーとデバッガ出力にも書き込みます。
自動撮影モードではエラーダイアログを表示しません。対話モードでは、保存先の書き込み失敗が起きても描画を継続します。

## 参考

- [Agility SDK公式ダウンロードとバージョン一覧](https://devblogs.microsoft.com/directx/directx12agility/)
- [Agility SDK導入ガイド](https://devblogs.microsoft.com/directx/gettingstarted-dx12agility/)
- [1.722-previewリリース情報](https://devblogs.microsoft.com/directx/agilitysdk-1-620-retail-and-1-722-preview/)
- [Dear ImGui v1.93.0](https://github.com/ocornut/imgui/tree/v1.93.0)
- [Dear ImGuiのWin32 / DirectX 12公式サンプル](https://github.com/ocornut/imgui/blob/v1.93.0/examples/example_win32_directx12/main.cpp)
