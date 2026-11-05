# サンプルの追加

`SampleFramework` がWin32メッセージループ、D3D12デバイス、スワップチェーン、ImGui、GPU同期、撮影を担当します。
サンプルは継承せず、`common/Sample.h` の `Sample` に必要なコールバックを設定します。
最小の例は `Clear/Main.cpp`、DXRの例は `DxrCube/` です。
CLASの実装を自分で追加するための検証環境は [`ClasSandbox/`](ClasSandbox/README.md) です。meshlet入力、通常DXRの比較描画、対応状況の診断、空のCLAS実装箇所を用意しています。

同じ土台で [Cluster Templates](ClusterTemplatesSandbox/README.md) と [PTLAS](PtlasSandbox/README.md) を個別に学べます。それぞれのプロジェクトに手続き生成の初期シーンと実験コードの入口があります。

1. `samples/MySample/` に `Main.cpp` と以下の `MySample.vcxproj` を配置します。GUIDは新しく生成してください。
2. ルートの `PathTracingSample.slnx` の `/Samples/` に `<Project Path="samples/MySample/MySample.vcxproj" />` を追加します。
3. ソリューションのNuGetパッケージを復元し、新しいプロジェクトをスタートアップに指定します。
4. 描画クラスやシェーダーを追加したら、プロジェクトのソース一覧にも追加します。

```xml
<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <PropertyGroup Label="Globals">
    <ProjectGuid>{新しいGUID}</ProjectGuid>
  </PropertyGroup>
  <Import Project="..\..\build_support\Cpp.props" />
  <ItemGroup>
    <ClCompile Include="Main.cpp" />
    <ProjectReference Include="..\..\common\SampleFramework.vcxproj">
      <Project>{15AB62DB-3209-43ED-874F-71EFA2815BAA}</Project>
    </ProjectReference>
  </ItemGroup>
  <Import Project="..\..\build_support\Cpp.targets" />
</Project>
```

```cpp
#include "Sample.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
	Sample sample;
	sample.mName = L"My Sample";
	return runSample(instance, showCommand, sample);
}
```

`Sample` のコールバックはすべて省略可能です。状態を保持するオブジェクトをMainで作り、ラムダで参照します。
そのオブジェクトは `runSample()` が戻るまで生存させてください。

| メンバ | 用途 |
| --- | --- |
| `mName` | ウィンドウのタイトル |
| `mRequireDxr` | DXR必須ならtrue。対応GPUを選択し、存在しない場合は明示的にエラー終了 |
| `mConfigure(arguments)` | 共通の `--screenshot` を除いたコマンドライン引数をウィンドウ作成前に処理 |
| `mReadyForScreenshot()` | 非同期処理完了後に撮影する必要があれば設定。trueになるまで自動撮影を待機 |
| `mInitialize(Renderer&)` | デバイス・ImGui初期化後に1回実行。サンプルのリソースを作成 |
| `mUpdate(Renderer&, deltaTime)` | ImGuiのNewFrame後、UI構築前に入力と状態を更新 |
| `mDrawGui()` | 共通パネル内にサンプル固有のコントロールを配置 |
| `mRender(Renderer&, commandList, backBuffer)` | 共通の画面クリア後、ImGui描画前にGPUコマンドを記録 |

`mRender` のバックバッファは呼び出し前後とも `RENDER_TARGET` 状態にします。
リソース状態の遷移をサンプル内で完結させ、ImGuiの描画は共通側へ任せてください。
幅・高さは `renderer.width()` / `height()` で取得し、変更時にはサイズ依存リソースも更新します。
共通Rendererは毎フレームGPU完了を待つため、次フレームでサイズ依存リソースを安全に作り直せます。
初期化時に別のコマンドリストを投入する場合も、ローカルなスクラッチ／アップロードリソースの解放前に `renderer.waitForGpu()` を呼びます。

ビルド設定、Agility SDKのヘッダー／DLL配置は共通props / targetsを使用し、各プロジェクトへコピーしません。
インクルードパスには `common/` と `third_party/imgui/` が設定されています。
実行ファイル、シェーダー、撮影先はプロジェクト別に分離されます。
DXRシェーダーのDXCカスタムビルドは `DxrCube.vcxproj` を参考にしてください。

カメラは `common/Camera.h` の `Camera` と `CameraInput` を再利用できます。
カメラ本体はWin32 / ImGuiから独立しており、サンプル側でUIの入力キャプチャ状態を確認してアクションを渡します。
Raw Inputの相対マウス移動は `renderer.mouseMotion()` で取得できます。共通の入力振り分けには `common/CameraController` を使用できます。
`DxrCube::update()` と `UsdKitchen::update()` が実例です。
