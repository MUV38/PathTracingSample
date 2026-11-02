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

各条件を131,072サンプルで積分します。88条件を白色環境・有限の矩形発光面それぞれについて、BRDF単独とNEE / MISの2方式で評価し、合計352条件のRGBを検査します。

- 白いBase Color: Metallic = 0 / 0.5 / 1、Roughness = 0 / 0.02 / 0.1 / 0.3 / 0.6 / 1、N・V = 1 / 0.5 / 0.1 / 0.02。
- 上記4つの視線角で、白いLambert、色付きLambert、黒い吸収体、Specular = 1の非金属も検査します。
- 法線と幾何法線は一致、AO = 1、発光・直接照明なしとします。棄却サンプルは寄与0として数え、再抽選しません。

`sampleBsdf` の平均重みが半球反射率です。NaN / Inf、負の重み、不正な反射方向を失敗にします。
反射率の上限は `1.002 + 6 × 標準誤差`、下限は `-0.0001` です。
完全鏡の白い金属は1、Specular = 0のLambertはBase Color、吸収体は0に対して絶対誤差0.0005以内を要求します。NEE / MISには光源サンプルの分散が加わるため、この既知値比較に6標準誤差を加えます。

Roughness = 0または0.6以上、およびLambert対照条件では、余弦半球サンプリングで `evaluateBsdf` を独立に積分します。
完全鏡のdelta成分は解析的なFresnel値を加えます。
この基準積分と通常サンプリングの差が `0.005 + 6 × 両者の標準誤差の二乗和平方根` 以内か検査します。
狭いGGXローブでは余弦サンプリングの分散が大きいため、この比較を省きます。

NEE / MISの条件は `mis-` 接頭辞で記録します。実描画と共通の `EnvironmentSampling.hlsli` を使用し、
明るい1画素を置いた16×8マップからCPUで作った非一様CDFをGPUで逆変換します。
照明の積分対象は単位白色のままにすることで、重点サンプリング分布・回転0.7ラジアン・立体角PDF・MISの重み付けを合わせても積分値が偏らないことを検査します。
全88条件で、NEE / MISとBRDF単独の差も `0.005 + 6 × 両者の標準誤差の二乗和平方根` 以内を要求します。
同じ比較を有限矩形の88条件でも行います。矩形条件は `rectangle-` / `mis-rectangle-` 接頭辞です。
2×2の発光面を表面の正面2 mに置き、放射輝度1、矩形の外は0とします。交差・区間外・平行レイ・裏面・PDFの既知値も検査します。
Lambertは解析的な矩形の形態係数（約0.23946）×Base Color、完全鏡は視線角に応じた0または1を基準にします。
矩形は有限サンプルのばらつきがあるため、既知値比較に6標準誤差を加えます。余弦半球による基準積分も矩形への到達を検査し、delta反射は別に加えます。
描画と影の比較は [RunRectangleLight](RectangleLight.md) を参照してください。

`furnace.csv` に反射率、標準誤差、基準積分、最大サンプル重み、不正サンプル数、合否を出力します。
このテストにより、従来の半角ベクトルのFresnelだけで拡散項を抑制した実装では、斜め視線で約1.85倍の反射率が生じることを検出しました。
拡散項へ視線側・入射光側の透過率を適用する修正後は、同じ条件・閾値で成功します。

単一散乱GGXと簡略化した拡散層のモデルなので、粗い面や非金属にエネルギー損失は残ります。
すべての白いマテリアルが必ず反射率1になることは要求しません。多重散乱補償、全マテリアル値・法線の組み合わせの証明は対象外です。

## 実描画の検証

実行時に自作の2三角形・1 meshletの平面と、単位放射輝度の1×1 Radiance HDRを生成します。
UsdKitchenで直接光・補助光を0、IBL強度1、反射1回、128サンプル以上で描画します。

- `mirror.png`: 白い完全鏡の金属。
- `diffuse.png`: 白いLambert（Specular = 0）。
- `absorber.png`: 黒い吸収体。物体が実際に描かれていることを確認する対照画像。

ImGuiのない画像右側を2画素間隔で検査します。鏡・拡散面は背景と一致し、吸収体には黒い面と白い背景の両方が必要です。
線形の放射輝度1はExposure = 1、ReinhardとsRGB変換後に約188となるため、白い画素数はRGB各188±1で数えます。
NEEの有限サンプルでは拡散面にノイズが生じるため、鏡・拡散面は検査範囲全体のRGB平均誤差が±1以内、RMSEが5以内であることを要求します。
白い画素は1,000以上必要です。吸収体は白・黒それぞれ1,000画素以上を要求します。
`images.json` に画素数・平均誤差・RMSEとPNGパスを記録します。保存PNGにはImGuiも含まれます。
これはCLI・描画・画像出力の検証であり、マウスやキーボードの操作確認を兼ねません。
