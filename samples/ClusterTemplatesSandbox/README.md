# Cluster Templates 学習プロジェクト

Visual Studio 2026でルートの `PathTracingSample.slnx` を開き、`ClusterTemplatesSandbox` をスタートアッププロジェクトにします。Agility SDK 1.722.0-preview、共通カメラ、ImGui、PIXマーカー、撮影機能を使います。

起動時は通常DXRの比較表示で、波状の地形パッチが見えます。4×4個のパッチを生成します。各パッチは25頂点・32三角形で、同じローカルインデックス列を共有できる形です。`Main.cpp` の `createPatchScene()` で頂点位置を調整できます。

```powershell
.\build\x64\Debug\ClusterTemplatesSandbox\ClusterTemplatesSandbox.exe
.\build\x64\Debug\ClusterTemplatesSandbox\ClusterTemplatesSandbox.exe --screenshot build/checks/clustertemplates.png
```

`--model <file.ptscene>` で任意の変換済みシーンを1インスタンスで表示できます。`--vertex-colors` で元の頂点色を使い、`--no-preview` で通常DXRの経路を確認できます。

## 実験コード

`ClusterTemplatesExperiment.h/.cpp` に処理を追加します。`initialize()` でトポロジーと頂点入力を用意し、`record()` に `BUILD_CLUSTER_TEMPLATES_FROM_TRIANGLES` → `INSTANTIATE_CLUSTER_TEMPLATES` → `BUILD_BLAS_FROM_CLAS` → 通常TLASの構築を追加します。`traceState()` で完成したTLASと対応PSO、シェーダーテーブルを返します。

実験の描画リソースが揃うと、ImGuiの `Use user implementation` から切り替えられます。起動時の比較表示は通常BLAS/TLASです。実験側の構築・更新・シェーダーはここから書き始められます。

`initialize()` は対応状況にかかわらず呼ばれるため、preview APIの前に `context.mCapabilities.available()` を確認します。`record()` は対応環境で描画前に呼ばれます。共通のroot signature、GPU入力のレイアウトと状態、シェーダーテーブルの契約は [ClasSandboxの説明](../ClasSandbox/README.md#gpu入力と描画契約) を参照してください。context自体は一時オブジェクトです。

ユーザー作成のSM 6.10シェーダーをビルドする場合は、`build_support/ClusterTemplates.local.props` に、手元にあるDXCとソースを指定できます。

```xml
<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
	<PropertyGroup>
		<ClusterTemplatesDxcExecutable>C:\Tools\DXC-preview\bin\x64\dxc.exe</ClusterTemplatesDxcExecutable>
		<ClusterTemplatesShaderSource>$(ProjectDir)ClusterTemplates.hlsl</ClusterTemplatesShaderSource>
	</PropertyGroup>
</Project>
```

出力は実行ファイルと同じフォルダの `ClusterTemplates.dxil` です。preview利用の前提条件とAPI仕様は [MicrosoftのDXRプレビュー資料](https://devblogs.microsoft.com/directx/clas-ptlas-preview/) を参照してください。
