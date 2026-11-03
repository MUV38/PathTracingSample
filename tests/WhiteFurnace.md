# ホワイトファーナステスト

全方向から線形RGB `(1, 1, 1)` の光が入る環境で、反射光が入射光を上回らないことを確認します。
`WhiteFurnace.hlsl` は実描画と同じ `samples/UsdKitchen/Pbr.hlsli` を直接インクルードします。
テスト専用のBRDF実装への置き換えや、反射率を1に丸める処理は行いません。

## 実行

Visual Studio 2026でルートの `PathTracingSample.slnx` をDebug / Releaseの両構成でビルドし、リポジトリ直下から実行します。

```powershell
.\tests\RunWhiteFurnace.ps1 -Configuration Debug
.\tests\RunWhiteFurnace.ps1 -Configuration Release
```

出力先はGit管理外の `build/checks/white-furnace-<構成>/` です。`-OutputDirectory` で変更できます。
スクリプトは数値テストを実行した後、UsdKitchenを起動して鏡・拡散面・吸収体のPNGを撮影し、画素を判定します。
失敗時は例外で停止します。PNGは開いて目視でも確認してください。

数値テストだけを実行する場合:

```powershell
.\build\x64\Debug\WhiteFurnaceTests\WhiteFurnaceTests.exe build/checks/furnace-numeric
.\build\x64\Release\WhiteFurnaceTests\WhiteFurnaceTests.exe build/checks/furnace-warp --warp
```

数値テストはDirectX 12 / Shader Model 6.0を使い、DXRは不要です。`--warp` はWARPを明示選択します。
通常はハードウェアを優先し、利用できなければWARPに切り替えます。成功時は終了コード0、失敗時は1と標準エラーを返します。
スクリプトの `-Warp` は数値テストにだけ適用され、画像検証にはDXR Tier 1.1対応GPUが必要です。
OpenUSD SDK、Python、外部モデルの取得は不要です。

## 数値検証

各条件を131,072サンプルで積分します。172マテリアル・視線条件について、多重散乱補償あり／なし、白色環境／有限矩形／発光メッシュ、BRDF単独／NEE・MIS、VNDF／NDFを組み合わせ、合計4,128条件のRGBを検査します。
NDF分岐はテストシェーダーの `PBR_COMPARE_NDF` 定義でだけ有効になり、実描画には入りません。`single-` を含む条件は補償なしです。

- 白いBase Color: Metallic = 0 / 0.5 / 1、Roughness = 0 / 0.02 / 0.1 / 0.17 / 0.3 / 0.45 / 0.6 / 0.83 / 1、N・V = 1 / 0.5 / 0.1 / 0.02。LUTの格子点に一致しない粗さも含めます。
- 上記4つの視線角で、白いLambert、色付きLambert、黒い吸収体、Specular = 1の非金属も検査します。
- 色付きBase Color = (0.95, 0.5, 0.1)、Metallic = 0 / 0.5 / 1、Specular = 1、Roughness = 0.02 / 0.3 / 0.6 / 1も4視線角で検査します。
- 法線と幾何法線は一致、AO = 1、発光・直接照明なしとします。棄却サンプルは寄与0として数え、再抽選しません。

`sampleBsdf` の平均重みが半球反射率です。NaN / Inf、負の重み、不正な反射方向を失敗にします。
反射率の上限は `1.002 + 6 × 標準誤差`、下限は `-0.0001` です。
完全鏡の白い金属は1、Specular = 0のLambertはBase Color、吸収体は0に対して絶対誤差0.0005以内を要求します。NEE / MISには光源サンプルの分散が加わるため、この既知値比較に6標準誤差を加えます。
補償ありの白い金属は、粗さが非ゼロでも白色環境の反射率1に対して `0.002 + 6 × 標準誤差` 以内を要求します。余弦サンプルを含む混合推定の分散とLUTの離散化誤差を考慮した閾値です。有限発光面の白い粗面金属は既知値1の対象ではありません。
全条件で固定の入射方向と視線を交換したBRDFも評価し、相反性を絶対・相対誤差0.0001で検査します。

Roughness = 0または0.6以上、およびLambert対照条件では、余弦半球サンプリングで `evaluateBsdf` を独立に積分します。
完全鏡のdelta成分は解析的なFresnel値を加えます。
この基準積分と通常サンプリングの差が `0.005 + 6 × 両者の標準誤差の二乗和平方根` 以内か検査します。
狭いGGXローブでは余弦サンプリングの分散が大きいため、この比較を省きます。

NEE / MISの条件は `mis-` 接頭辞で記録します。実描画と共通の `EnvironmentSampling.hlsli` を使用し、
明るい1画素を置いた16×8マップからCPUで作った非一様CDFをGPUで逆変換します。
照明の積分対象は単位白色のままにすることで、重点サンプリング分布・回転0.7ラジアン・立体角PDF・MISの重み付けを合わせても積分値が偏らないことを検査します。
全条件で、NEE / MISとBRDF単独の差も `0.005 + 6 × 両者の標準誤差の二乗和平方根` 以内を要求します。
同じ比較を有限矩形でも行います。矩形条件は `rectangle-` / `mis-rectangle-` 接頭辞です。
2×2の発光面を表面の正面2 mに置き、放射輝度1、矩形の外は0とします。交差・区間外・平行レイ・裏面・PDFの既知値も検査します。
Lambertは解析的な矩形の形態係数（約0.23946）×Base Color、完全鏡は視線角に応じた0または1を基準にします。
矩形は有限サンプルのばらつきがあるため、既知値比較に6標準誤差を加えます。余弦半球による基準積分も矩形への到達を検査し、delta反射は別に加えます。
描画と影の比較は [RunRectangleLight](RectangleLight.md) を参照してください。
発光メッシュ条件は `mesh-` / `mis-mesh-` 接頭辞です。CPUのMeshLight.cppで生成したCDFをGPUへ転送し、実描画と共通のMeshLight.hlsliで三角形選択・面積サンプリング・立体角PDFを検査します。
2三角形の発光領域は矩形と等しいため、Lambertの解析解と鏡の既知値は矩形条件と同じです。光源表の法線を反転した側から積分し、既存マテリアルの両面発光とPDFが一致することも確認します。
全条件でMIS有無の平均値を既存の許容誤差で比較します。異なる面積・輝度・編集後のCDFはMeshLightTestsで検証します。

`furnace.csv` に反射率、標準誤差、基準積分、最大サンプル重み、不正サンプル数、分散、棄却率、合否を出力します。
`ndf-` 接頭辞の条件は従来NDFを使い、対応するVNDFとの平均値の差が `0.005 + 6 × 両者の標準誤差の二乗和平方根` 以内か検査します。両方式とも各自のPDFをMISに使います。
白い金属・Roughness = 0.6・N·V = 0.02・白色環境・BRDF単独の条件では、VNDFの分散と棄却率がNDFの1/10未満であることも要求します。棄却率は `sampleBsdf` がfalseを返した割合で、吸収体など寄与0の条件も含みます。有限光源に当たらないサンプルの割合とは区別します。
このテストにより、従来の半角ベクトルのFresnelだけで拡散項を抑制した実装では、斜め視線で約1.85倍の反射率が生じることを検出しました。
数値実行の最後には [Russian Rouletteの14条件](RussianRoulette.md) も検証し、別の `roulette.csv` へ記録します。BRDFの4,128条件自体にはRRを適用しません。
続いて [ReSTIRのReservoir 32条件](RestirDI.md) を検証し、`restir.csv` へ記録します。
IBLのReservoirについても、通常CDFと確率0を含むCDFで各80条件を検証し、`restir-environment.csv` / `restir-environment-sparse.csv` へ記録します。
拡散項へ視線側・入射光側の透過率を適用する修正後は、同じ条件・閾値で成功します。

多重散乱は [Kulla–ContyのLUT近似](../tools/GgxEnergyLut/README.md) です。色付き金属には吸収があり、非金属の拡散層も簡略化しているため、すべての白いマテリアルが必ず反射率1になることは要求しません。
微小面のランダムウォークや、全マテリアル値・法線の組み合わせの証明は対象外です。

## 実描画の検証

実行時に自作の2三角形・1 meshletの平面と、単位放射輝度の1×1 Radiance HDRを生成します。
UsdKitchenで直接光・補助光を0、IBL強度1、反射1回、128サンプル以上で描画します。

- `mirror.png`: 白い完全鏡の金属。
- `rough-metal.png`: 白い金属、Roughness = 1、多重散乱補償あり。
- `rough-bsdf.png`: 同じ粗面金属、補償あり・IBL MISなし。
- `rough-single.png`: 同じ粗面金属、補償なし。暗い面（RGB各170未満）と白い背景が各1,000画素以上あることを要求します。
- `diffuse.png`: 白いLambert（Specular = 0）。
- `absorber.png`: 黒い吸収体。物体が実際に描かれていることを確認する対照画像。

ImGuiのない画像右側を2画素間隔で検査します。鏡・拡散面は背景と一致し、吸収体には黒い面と白い背景の両方が必要です。
線形の放射輝度1はExposure = 1、ReinhardとsRGB変換後に約188となるため、白い画素数はRGB各188±1で数えます。
有限サンプルではノイズが生じるため、鏡・補償あり粗面金属（MIS有無とも）・拡散面は検査範囲全体のRGB平均誤差が±1以内、RMSEが5以内であることを要求します。
白い画素は1,000以上必要です。吸収体は白・黒それぞれ1,000画素以上を要求します。
`images.json` に画素数・平均誤差・RMSEとPNGパスを記録します。保存PNGにはImGuiも含まれます。
これはCLI・描画・画像出力の検証であり、マウスやキーボードの操作確認を兼ねません。
