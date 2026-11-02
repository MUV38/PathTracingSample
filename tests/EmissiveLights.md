# 発光メッシュの検証

ルートの `PathTracingSample.slnx` をDebug / Releaseでビルドし、リポジトリ直下で実行します。

```powershell
.\tests\RunEmissiveLights.ps1 -Configuration Debug
.\tests\RunEmissiveLights.ps1 -Configuration Release
.\build\x64\Debug\WhiteFurnaceTests\WhiteFurnaceTests.exe build/checks/emissive-numeric-Debug
.\build\x64\Release\WhiteFurnaceTests\WhiteFurnaceTests.exe build/checks/emissive-numeric-Release
```

RunEmissiveLightsはCPUテストを実行して自作 `.ptscene` を生成し、UsdKitchenで8種類のPNGを撮影・判定します。
出力先はGit管理外の `build/checks/emissive-<構成>/` です。`-OutputDirectory` で変更できます。
数値積分テストは [WhiteFurnace.md](WhiteFurnace.md) の528条件に含み、`--warp` でも実行できます。
表示にはDXR Tier 1.1が必要です。外部モデル、OpenUSD SDK、Python、追加ソフトウェアの取得は不要です。

## CPUテスト

`MeshLightTests.exe <出力先>` でGPUなしで実行できます。

- 三角形の面積、面積×発光輝度による選択確率、CDFの正規化と面積PDFの一致。
- 非発光三角形とゼロ面積の除外、全消灯時の空分布。
- 発光色・強度の変更、消灯、非発光マテリアルの発光化によるCDF更新。
- 不正なインデックス、マテリアル、非有限座標の拒否。
- 複数meshletに分かれた自作モデルのバイナリ往復と、発光三角形数の確認。

## 描画テスト

IBL・方向光・補助光を0にし、発光メッシュだけで照らします。専用の床は白いLambertです。
発光面は黒いBase Color、Specular = 0とし、通常の反射と発光の寄与を区別しやすくしています。

| 画像 | 条件・確認内容 |
| --- | --- |
| mis / bsdf | 白い2×2 mの発光面、放射輝度4。1024サンプルでNEE/MIS有無の明るさとノイズを比較 |
| shadow | 256サンプル。床と発光面の間の遮蔽物によるソフトシャドウ |
| multiple / multiple-bsdf | 暖色・青色の別マテリアルと別meshletの発光面。256サンプルで両方の色の照明を確認 |
| mirror / mirror-bsdf | 128サンプル。完全鏡への映り込みがMISで暗くならないことを比較 |
| dark | 128サンプル。発光強度0のモデルで全画素が黒になることを確認 |

各撮影の終了コード0と、右側60%以降を2画素間隔で検査します。
床の判定領域は画像の下側45%です。通常画像に照明または反射が200画素以上、鏡にR > 220の画素が100以上必要です。
複数光源ではR > 30かつR > 1.1×Bの暖色画素と、B > 30かつB > 1.1×Rの青色画素を各100以上要求します。
単一白色光源のMIS有無で床領域の平均R値の差が1以内、全消灯では検査領域のRGB最大値が0であることを要求します。
`images.json` に集計とPNGパスを保存します。画素数だけでは影の形を保証しないため、保存PNGも開いて確認してください。
この自動確認はCLI・描画・保存画像の検証です。UIのマウス・キーボード操作は別に確認する必要があります。

## 利用と対応範囲

生成モデルは次のように表示できます。

```powershell
.\build\x64\Release\UsdKitchen\UsdKitchen.exe --model build/checks/emissive-Release/multiple.ptscene --light-strength 0 --ibl-intensity 0
```

`PBR material` で既存のEmissive Color / Strengthを編集します。`Indirect lighting / IBL` のEmissive NEE / MISは既定で有効です。
`--no-emissive-mis` は比較用に発光メッシュのNEE/MISだけを無効化します。
発光色×強度は定数の線形HDR放射輝度で、既存のマテリアル仕様に合わせて両面へ発光します。
IBL、解析的な矩形ライト、点光源・方向光と併用できます。メッシュ発光の影は常に有効で、直接光のShadows設定とは独立です。
発光面も通常のBRDF反射を持ちます。MISで重み付けするのは発光成分だけです。

`.ptscene` はv3のままで、変換ツールは変更しません。表示時にmeshlet / triangle順の光源表を派生生成します。
全三角形の位置・辺・面積・マテリアル番号を保持し、発光色・強度の変更時にCDFと放射輝度を更新します。
光源選択のCDFとPDFはfloat丸め後の確率を一致させます。丸めで確率が0になった発光面はBRDFサンプリング側で扱います。
非発光三角形も含む64 byte/三角形の表をCPUとGPUに保持します。発光変更は三角形数に比例する更新です。
ゼロ面積はサンプリングから除外し、面積PDFが有限値で表せないほど小さい三角形はエラーにします。
テクスチャ発光、片面/両面の個別設定、USDライトの読み込み、動的なメッシュ変形には未対応です。

参考: [PBRT Area Lights](https://pbr-book.org/4ed/Light_Sources/Area_Lights)、[PBRT A Better Path Tracer](https://pbr-book.org/4ed/Light_Transport_I_Surface_Reflection/A_Better_Path_Tracer)
