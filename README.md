# PathTracingSample

DXRによるPath Tracing実装に向けた、Win32 / DirectX 12の最初のステップです。
現段階では1280 × 720のウィンドウを開き、バックバッファを濃い青色でクリアして表示します。

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

`src/Main.cpp` がウィンドウとメッセージループ、`src/Renderer.cpp` がDirectX 12の初期化と描画を担当します。
クリア色は `Renderer::render()` の `clearColor` で変更できます。

## 参考

- [Agility SDK公式ダウンロードとバージョン一覧](https://devblogs.microsoft.com/directx/directx12agility/)
- [Agility SDK導入ガイド](https://devblogs.microsoft.com/directx/gettingstarted-dx12agility/)
- [1.722-previewリリース情報](https://devblogs.microsoft.com/directx/agilitysdk-1-620-retail-and-1-722-preview/)
