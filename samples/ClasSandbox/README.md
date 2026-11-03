# CLAS検証用の土台

`PathTracingSample.slnx` の `ClasSandbox` をスタートアッププロジェクトにして実行します。
Agility SDK **1.722.0-preview**、C++20、共通SampleFrameworkを使用します。
USD SDK、Streamline、NVAPIへの依存はありません。追加ソフトは自動ダウンロードしません。

用意しているのは次の部分です。

- Win32 / DX12 / ImGui、スクリーンショット、既存のPIXキャプチャとマーカー
- `.ptscene` v3の読み込みとmeshletデータのGPU転送
- 比較表示用の通常DXR BLAS / TLAS、一次レイの簡易照明、meshlet色表示
- 共通フリーカメラ（RMB + WASD、Q/E、ホイール速度、Alt + マウスのorbit / pan / dolly）
- デバイス生成前のpreview機能有効化、CLASとSM 6.10対応状況の表示
- ユーザーがCLAS処理を追加する `ClasExperiment.h/.cpp`

**CLASの構築・Cluster BLASの組み立て・CLAS用シェーダーは未実装です。**
起動時の `Reference DXR` は従来方式です。CLASの動作や性能を示す結果ではありません。
CLAS対応環境でも、ユーザー実装が描画リソースを返すまでは切り替えチェックボックスを無効にしています。

## 実行

```powershell
# assets/local のKitchen Setがあれば優先し、なければ同梱Preview Room
& .\build\x64\Debug\ClasSandbox\ClasSandbox.exe

# 小さい自作シーンで開始
& .\build\x64\Debug\ClasSandbox\ClasSandbox.exe --model assets/PreviewRoom.ptscene

# meshlet色ではなく元の頂点色を表示
& .\build\x64\Release\ClasSandbox\ClasSandbox.exe --vertex-colors

# previewを有効化せず、通常DXR経路だけ確認
& .\build\x64\Debug\ClasSandbox\ClasSandbox.exe --no-preview

# 共通の撮影オプションも利用可能
& .\build\x64\Debug\ClasSandbox\ClasSandbox.exe --model assets/PreviewRoom.ptscene --screenshot build/checks/clas.png
```

`--model` はカレントディレクトリ基準です。モデルの変更は起動し直して行います。
原本USDの変換には既存の独立した `tools/UsdConverter/` を使用してください。
64頂点 / 126三角形の既存meshletをそのまま使用し、表示時の再分割は行いません。

## 対応状況

`CLAS diagnostics` に、experimental featuresの有効化、Raytracing2機能照会、
device / command-listインターフェース取得のHRESULT、`ClustersAndPTLASSupported`、SM 6.10を表示します。
一つでも前提を満たさなければ通常DXRだけで動作します。DXR対応自体は必須です。

Windowsの開発者モードと対応ドライバーが必要です。OS設定・ドライバーの変更は行いません。
preview有効化には `D3D12ExperimentalShaderModels` と `D3D12RaytracingPreview` を使用し、
`ID3D12Device16::CheckFeatureSupport1` に `D3D12_PREVIEW_SDK_VERSION` を渡します。
APIの前提条件は [Microsoftの公式説明](https://devblogs.microsoft.com/directx/clas-ptlas-preview/) を参照してください。

## CLASを書く場所

`ClasExperiment.cpp` の3箇所を入口にしています。必要な所有リソースは `ClasExperiment.h` に追加します。

1. `initialize(context)`：CPU入力の参照、prebuildサイズ照会、CLAS用リソースの確保など。
   対応していない環境でも呼び出すため、preview APIを使う前に `context.mCapabilities.available()` を確認します。
2. `record(context, commands)`：CLAS、Cluster BLAS、TLASの構築や必要なバリアなど。
   対応環境ではReference表示中も毎フレーム、描画前に呼びます。初回構築をここで行うこともできます。
3. `traceState()`：完成したTLAS、CLAS対応PSO、シェーダーテーブルを返します。
   3つすべてが非nullになるとUIからCLAS表示を選択できます。

`ClasContext` は共有root signature、Renderer（device / queue / waitForGpu）、CPUのSceneData、
GPU入力を参照します。context自体は一時オブジェクトなので保存しないでください。
GPU入力はサンプルが所有し、シーンの読み込み後は不変です。
`traceState()` は借用ポインターを返すため、返すリソースを実装クラスで所有してください。
追加コマンドを初期化時に投入する場合も、一時リソースの破棄前にGPU完了を待ってください。

フレームワークは毎フレームGPU完了を待ちます。非同期処理やフレームをまたぐ最適化はこの土台には含みません。
`record()` が描画するTLASを構築した場合、後続のDispatchRaysで参照できるように順序と状態を整えてください。
実装内で変更した入力バッファの状態も、呼び出し終了時に復元してください。

## GPU入力と描画契約

`MeshletGpuInputs` の各バッファはDEFAULTヒープ、`NON_PIXEL_SHADER_RESOURCE` 状態です。
CLASの頂点位置配列の並べ方や引数生成はユーザー実装側で決めてください。
`mVertexReferences` はグローバル頂点番号、`mTriangleBytes` はmeshlet内のローカル頂点番号です。
頂点参照を解決せずにグローバル頂点バッファへローカル三角形番号を使うことはできません。

| root parameter | shader binding | 内容 |
| --- | --- | --- |
| 0 | u0 | RGBA8出力UAVのdescriptor table |
| 1 | t0 | 描画するTLAS |
| 2 | b0 | SceneConstants、24 DWORD |
| 3 | t1 | SceneVertex、40バイトstride |
| 4 | t2 | SceneMeshlet、16バイトstride（uint4） |
| 5 | t3 | meshletの頂点参照、uint32 |
| 6 | t4 | meshletの三角形ローカル番号、uint8 / ByteAddressBuffer |
| 7 | t5 | SceneMaterial、48バイトstride |

三角形バッファは最後を4バイト境界までゼロ埋めします。CPU側の論理サイズとオフセットは変えません。
共通の宣言は `SceneBindings.hlsli`、通常DXRの描画例は `Reference.hlsl` です。
シェーダーテーブルは64バイトずつのraygen / miss / triangle hit-group、計3レコードを返してください。
TLASはSRVとしてバインドできる状態、PSOはこのroot signatureと出力契約を使用してください。
共通側がこれらをバインドし、画像のバックバッファへのコピーとImGui合成を行います。

Referenceでは `GeometryIndex()` がmeshlet番号、`PrimitiveIndex()` がmeshlet内の三角形番号です。
全geometryで同じhit-groupを共有するため、TraceRayのgeometry contribution multiplierは0です。
CLAS側のID付与とシェーダー側の検索方法はご自身で実装してください。
CLASを通るPSOのpipeline flagや、必要なSM 6.10 shader intrinsicsの追加も未実装範囲です。
簡易照明は頂点色またはmeshlet色で、PBRパストレーサーや影の実装ではありません。

## ユーザーシェーダーのビルド設定

Referenceの `lib_6_5` は既存Windows SDKのDXCでビルドします。
CLAS用のSM 6.10には、公式に案内されている
[DXC 1.10.2605.37-preview](https://www.nuget.org/packages/Microsoft.Direct3D.DXC/1.10.2605.37-preview)
などの対応コンパイラーが別途必要です。今回追加で取得・インストールしていません。

コンパイラーを用意してCLAS用のHLSLを書いた後、Git管理対象外の `build_support/Clas.local.props` に指定できます。
ファイルがない場合はReferenceだけをビルドします。

```xml
<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
	<PropertyGroup>
		<ClasDxcExecutable>C:\Tools\DXC-preview\bin\x64\dxc.exe</ClasDxcExecutable>
		<ClasShaderSource>$(ProjectDir)Clas.hlsl</ClasShaderSource>
	</PropertyGroup>
</Project>
```

指定したソースを `lib_6_10`、HLSL 2021、警告をエラー、埋め込みデバッグ情報付きで
出力先の `Clas.dxil` へコンパイルします。追加のhlsliを作る場合はCustomBuildのAdditionalInputsも更新してください。
PSOの作成・ライブラリーの読み込みは `ClasExperiment` 側で実装します。
エントリーポイント名は `RGS_` / `RMS_` / `CHS_` / `CS_` などのステージ接頭辞を使用してください。

## 確認範囲

Debug / Releaseのソリューションビルドと、Preview Room、既定のKitchen Set、頂点色表示、
`--no-preview` の計8回の自動撮影を確認しています。各終了コードは0で、保存PNGも開いて確認しています。
RTX 3060 Tiの現在の環境ではCLAS機能照会とSM 6.10の照会に成功しました。
PIXの通常DXRキャプチャ・マーカー・デバッグレイヤー再生は `tests/RunPixCapture.ps1` に含めています。

CLASの構築・描画、ユーザーシェーダーのSM 6.10コンパイル、カメラ入力の手動操作は未検証です。
このプロジェクトで新たなCLAS実装やコンパイラーの取得は行っていません。
