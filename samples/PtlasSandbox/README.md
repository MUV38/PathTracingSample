# PTLAS 学習プロジェクト

Visual Studio 2026でルートの `PathTracingSample.slnx` を開き、`PtlasSandbox` をスタートアッププロジェクトにします。Agility SDK 1.722.0-preview、共通カメラ、ImGui、PIXマーカー、撮影機能を使います。

起動時は通常DXRの比較表示で、4グループの箱が見えます。1つの箱の通常BLASを、4グループ×4個の計16インスタンスで共有します。`context.mReferenceInstances` に通常TLAS用の変換行列、BLASアドレス、InstanceID、マスクを渡します。既定シーンでは4つずつ連続するIDを1グループとして扱えます。`Main.cpp` の `createInstanceOffsets()` で配置を調整できます。

```powershell
.\build\x64\Debug\PtlasSandbox\PtlasSandbox.exe
.\build\x64\Debug\PtlasSandbox\PtlasSandbox.exe --screenshot build/checks/ptlas.png
```

`--model <file.ptscene>` で任意の変換済みシーンを1インスタンスで表示できます。`--vertex-colors` で元の頂点色を使い、`--no-preview` で通常DXRの経路を確認できます。

## 実験コード

`PtlasExperiment.h/.cpp` に処理を追加します。`initialize()` で容量とパーティションの割り当てを決め、`record()` にPTLASの初回構築と疎な更新を追加します。移動を試す場合は `WRITE_INSTANCE` を使います。箱の通常BLASを再利用し、同じ描画契約を使う場合は `context.mReferenceTrace` のPSOとシェーダーテーブルを借用できます。`traceState()` からは実験側のPTLASを返します。既存シェーダーの法線は平行移動だけを想定しています。

実験の描画リソースが揃うと、ImGuiの `Use user implementation` から切り替えられます。起動時の比較表示は通常BLAS/TLASです。実験側の構築・更新・シェーダーはここから書き始められます。

`initialize()` は対応状況にかかわらず呼ばれるため、preview APIの前に `context.mCapabilities.available()` を確認します。`record()` は対応環境で描画前に呼ばれます。共通のroot signature、GPU入力のレイアウトと状態、シェーダーテーブルの契約は [ClasSandboxの説明](../ClasSandbox/README.md#gpu入力と描画契約) を参照してください。context自体は一時オブジェクトです。

ユーザー作成のSM 6.10シェーダーをビルドする場合は、`build_support/Ptlas.local.props` に、手元にあるDXCとソースを指定できます。

```xml
<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
	<PropertyGroup>
		<PtlasDxcExecutable>C:\Tools\DXC-preview\bin\x64\dxc.exe</PtlasDxcExecutable>
		<PtlasShaderSource>$(ProjectDir)Ptlas.hlsl</PtlasShaderSource>
	</PropertyGroup>
</Project>
```

出力は実行ファイルと同じフォルダの `Ptlas.dxil` です。preview利用の前提条件とAPI仕様は [MicrosoftのDXRプレビュー資料](https://devblogs.microsoft.com/directx/clas-ptlas-preview/) を参照してください。
