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

## meshletへの分割

同梱meshoptimizer v1.3の必要なC++ソースを直接コンパイルします。追加のインストールは不要です。
位置・法線・色・マテリアル番号の40バイト全体で頂点を重複除去し、`meshopt_buildMeshletsSpatial` でレイトレーシング向けに分割します。面ごとのマテリアル境界も保持します。
最大64頂点・126三角形、目標最小32三角形、fill_weight 0.5です。小さい形状や頂点数の制限によって32三角形未満にもなります。
上限は [NVIDIA公式のmeshlet推奨値](https://developer.nvidia.com/blog/introduction-turing-mesh-shaders/) に合わせています。Mesh Shader向けの推奨値を採用したもので、CLASでの最適サイズは別途評価が必要です。
分割はワールド空間へ展開したシーン全体を対象とします。LOD、階層クラスタ、カリングはまだ生成しません。

## 独自形式 v3（meshlet + PBRマテリアル）

little-endian、IEEE754 float32。右手系Y-up、ワールド座標、メートル単位です。
構造体と読み書きの共通定義は `common/SceneFile.h/.cpp` です。USDには依存しません。
v1 / v2は読み書きとも未対応です。PBR値を保存するためバージョンを更新しました。古い変換結果はUSDから再生成してください。

| オフセット | 型 | 内容 |
| --- | --- | --- |
| 0 | char[8] | `PTUSDM03` |
| 8 | uint32 | バージョン3 |
| 12 | uint32 | 頂点サイズ40 |
| 16 | uint64 | 共有頂点数 |
| 24 | uint64 | メッシュ出現数 |
| 32 | float[3] | 最小座標 |
| 44 | float[3] | 最大座標 |
| 56 | uint64 | 全ペイロードのFNV-1a 64bit |
| 64 | uint64 | meshlet数 |
| 72 | uint64 | 頂点参照数 |
| 80 | uint64 | ローカル三角形インデックスのバイト数（三角形数×3） |
| 88 | uint64 | マテリアル数（1～65536） |
| 96 | 配列 | 以下の5配列を順に保存 |

ペイロードの順序:

1. 共有頂点（40バイト）: position float3、normal float3、vertex color float3、materialIndex uint32。
2. マテリアル（48バイト）: baseColor float3、metallic float、emissiveColor float3、roughness float、specular float、occlusion float、emissiveStrength float、padding float（0固定）。
3. meshlet（16バイト）: vertexOffset、triangleOffset、vertexCount、triangleCount（すべてuint32）。
4. 頂点参照（uint32）: 共有頂点配列へのインデックス。
5. ローカル三角形インデックス（uint8）: 各meshlet内の頂点番号。3個で1三角形、パディングなし。

vertexOffsetは頂点参照配列の要素位置、triangleOffsetはuint8配列のバイト位置です。
各meshletの範囲は隙間なく並べ、ローカル番号はvertexCount未満、頂点参照は共有頂点数未満にします。
1三角形の3頂点は同じマテリアル番号を持ちます。チェックサムはこの5配列の連結全体を対象とします。

色・Metallic・Roughness・Specular・AOは0～1、Emissive Strengthは0～100000です。
USDの `diffuseColor`、`metallic`、`roughness`、`occlusion`、`emissiveColor` の定数値を変換します。
`ior` は `F0=((ior-1)/(ior+1))^2`、`Specular=clamp(F0/0.08,0,1)` へ変換します。既定IOR 1.5はSpecular 0.5です。
HDR emissiveColorは0～1の色と強度に分解して保存します。バインドとGeomSubsetの面割り当てを保持します。
定数Base Colorがあれば頂点色は白、なければdisplayColorを使います。未バインドのマテリアル0はBase Color白、Metallic 0、Roughness 0.5、Specular 0.5、AO 1、発光なしです。
USDの `useSpecularWorkflow=1` は未対応としてエラーにします。テクスチャ接続は評価せず、取得できる定数・フォールバック値だけを使います。

空シーン、3000万頂点・参照・ローカルインデックス超、不正バージョン、長さ不一致、非有限値、ゼロ法線、範囲外の参照、不正なmeshlet範囲、チェックサム不一致は拒否します。
出力は同じフォルダの一時ファイルから置換します。

## 検証

利用者が配置したPixar Kitchen Set（同梱VERSION.txtの2022-07-21版）は、通常版とUSDネイティブインスタンス版の両方をDebug / Releaseで変換・表示確認しています。
原本はGit管理外の `assets/Source/Kitchen_set/`、検証結果は `build/checks/kitchen-<構成>/` です。
`tests/RunKitchenSet.ps1 -Configuration <構成>` は変換終了コード、描画終了コード、通常版とインスタンス版の出力SHA256一致を検査し、統計・ログ・PNGを保存します。
保存PNGを開いて表示内容も確認してください。Subdivisionは既存仕様どおり制御ケージの出力です。

メインソリューションのSceneFileTestsはUSD SDK不要です。自作の凹多角形を変換した結果も追加引数で検証できます。
階層変換、cmからmへの変換、Z-upからY-upへの変換、法線、indexed displayColor、三角形の面積を確認します。
共有頂点を持つグリッドの分割、三角形・向き・法線・色・マテリアル番号の保存、meshlet上限・インデックス不正の拒否、旧形式の拒否もテストします。
PbrFixtureではマテリアルの面割り当て、境界の頂点分離、Metallic、Roughness、IOR、AO、HDR発光値を確認します。

```powershell
.\build\x64\Release\UsdConverter\UsdConverter.exe tests/ConversionFixture.usda build/checks/conversion.ptscene
.\build\x64\Release\UsdConverter\UsdConverter.exe tests/PbrFixture.usda build/checks/pbr-materials.ptscene
.\build\x64\Debug\SceneFileTests\SceneFileTests.exe build/checks build/checks/conversion.ptscene build/checks/pbr-materials.ptscene
.\build\x64\Release\SceneFileTests\SceneFileTests.exe build/checks build/checks/conversion.ptscene build/checks/pbr-materials.ptscene
```
