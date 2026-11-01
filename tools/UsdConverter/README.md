# 独立USD変換ツール

`UsdConverter.slnx` をVisual Studio 2026で開きます。レンダラーとは別のプロジェクト・ソリューションです。
新しいSDKのインストールやダウンロードは行いません。利用者が用意したWindows x64版OpenUSDを指定してください。
monolithic版の `include/pxr`、`lib/usd_ms.lib`、`lib/usd_ms.dll`、`lib/usd/`、oneTBBのlib / DLLを想定しています。
OpenUSD 26.05、MSVC v145でDebug / Releaseを確認しています。SDKの構成、CRT、ツールセットを合わせてください。

## SDKの指定

`build_support/Usd.example.props` を `build_support/Usd.local.props` にコピーし、ローカルのSDKを設定します（後者はGit管理外）。
Release用は `OpenUsdRoot`、Debug用は `OpenUsdDebugRoot` です。
環境変数 `OPENUSD_ROOT` / `OPENUSD_DEBUG_ROOT` またはMSBuildプロパティでも指定できます。

```powershell
msbuild tools/UsdConverter/UsdConverter.slnx /p:Configuration=Release /p:Platform=x64 /p:OpenUsdRoot=C:\SDK\OpenUSD-release
msbuild tools/UsdConverter/UsdConverter.slnx /p:Configuration=Debug /p:Platform=x64 /p:OpenUsdDebugRoot=C:\SDK\OpenUSD-debug
.\build\x64\Release\UsdConverter\UsdConverter.exe assets/Source/PreviewRoom/PreviewRoom.usda assets/PreviewRoom.ptscene
```

SDKのランタイムDLLとプラグインリソースをツールの出力先へコピーします。表示アプリにはコピーしません。
変換に失敗した場合は終了コード1で理由を出力し、既存の出力ファイルを維持します。
対応範囲は [表示サンプル](../../samples/UsdKitchen/README.md) を参照してください。

## 独自形式 v1

little-endian、IEEE754 float32。右手系Y-up、ワールド座標、メートル単位、非インデックス三角形リストです。
構造体と読み書きの共通定義は `common/SceneFile.h/.cpp` です。USDには依存しません。

| オフセット | 型 | 内容 |
| --- | --- | --- |
| 0 | char[8] | `PTUSDM01` |
| 8 | uint32 | バージョン1 |
| 12 | uint32 | 頂点サイズ36 |
| 16 | uint64 | 頂点数（三角形数×3） |
| 24 | uint64 | メッシュ出現数 |
| 32 | float[3] | 最小座標 |
| 44 | float[3] | 最大座標 |
| 56 | uint64 | 頂点ペイロードのFNV-1a 64bit |
| 64 | 頂点配列 | 各頂点はposition float3、normal float3、color float3 |

空シーン、3000万頂点超、不正バージョン、長さ不一致、非有限値、ゼロ法線、チェックサム不一致は拒否します。
出力は同じフォルダの一時ファイルから置換します。

## 検証

メインソリューションのSceneFileTestsはUSD SDK不要です。自作の凹多角形を変換した結果も追加引数で検証できます。
階層変換、cmからmへの変換、Z-upからY-upへの変換、法線、indexed displayColor、三角形の面積を確認します。

```powershell
.\build\x64\Release\UsdConverter\UsdConverter.exe tests/ConversionFixture.usda build/checks/conversion.ptscene
.\build\x64\Debug\SceneFileTests\SceneFileTests.exe build/checks build/checks/conversion.ptscene
.\build\x64\Release\SceneFileTests\SceneFileTests.exe build/checks build/checks/conversion.ptscene
```
