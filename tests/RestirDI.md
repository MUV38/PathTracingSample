# ReSTIR DI（発光メッシュ・IBL・矩形光源）

一次ヒットの発光メッシュ照明・IBL・矩形光源に、RISと時間・空間Reservoir再利用を使用します。外部SDKやライブラリは追加しません。
既定は従来のNEE / MISです。ImGuiの `ReSTIR DI` から選ぶか、CLIで切り替えます。

```powershell
.\build\x64\Release\UsdKitchen\UsdKitchen.exe --model build/checks/restir-Release/multiple.ptscene --restir spatial --samples 1024 --spp 4
```

モデルは下記の検証スクリプトで生成します。Kitchen Setの原本は使用しません。

| `--restir` | 処理 |
|---|---|
| `off` | 既存の発光メッシュ・IBL・矩形光源 NEE / MIS |
| `ris` | 発光メッシュとHDRの既存CDF、矩形の面積一様分布から合計8候補を生成し、1個のReservoirへ集約 |
| `temporal` | RISに前フレームのReservoirを追加 |
| `spatial` | 時間再利用後、半径8画素内から最大4近傍も統合 |

方向光・点光源、二次ヒット以降のNEE / MISは従来の方式です。`--no-emissive-mis` は発光メッシュ候補、`--no-ibl-mis` はIBL候補、`--no-rect-mis` は矩形候補を無効にし、その光源はBSDFサンプリングだけで評価します。解析的な矩形光源は従来どおり1灯です。
鏡のdelta反射は維持します。Roughness < 0.1では時間・空間再利用を省き、RISだけを使います。

## パスと履歴

有効時は同じDXRシェーダーを3回ディスパッチします。

1. カメラから一次ヒットを求め、G-bufferへ位置・視線方向の深度・シェーディング法線・幾何法線・Material ID・Triangle ID・Base Color・Roughness・モーションを保存します。RISと時間再利用を実行します。
2. 別のReservoirバッファへ空間再利用結果を書きます。近傍の入力を同じパスで上書きしません。
3. パストレーシングでReservoirを評価します。選択した光源への遮蔽は、現在の位置から毎回再トレースします。

G-bufferは前後2個、Reservoirは初期・最終・前フレームの3個で、パス間にUAVバリアを置きます。
有効時のバッファは1画素256バイト（1280×720で約225 MiB）です。ReSTIRとデバッグ表示がともに無効なときは各バッファを1要素だけ確保します。
G-buffer viewは照明方式と独立しています。NEE / MIS選択中や発光三角形0でも法線・深度・モーションを表示でき、デバッグ表示中はG-buffer用のバッファと生成パスを有効にします。ReSTIR自体は有効化しません。
現在の実装は学習・検証向けで、パス数・メモリ削減や実シーンの速度最適化は未実施です。

再投影には前フレームのView-Projection行列とviewportを使い、既存カメラの右手系の基底に合わせます。
画面外・カメラ背後を棄却し、位置差（許容値は0.01 mと深度の2%の大きい方）、両法線の内積 > 0.95、Material ID一致を検査します。
モデル・マテリアル・照明・設定・解像度の変更では履歴を無効にします。カメラ移動時は色の蓄積をリセットし、再投影可能なReservoirは保持します。
RIS用の乱数は経路サンプリングと分離し、カメラ移動で蓄積がリセットされてもフレームごとに更新します。

複数SPPでは、そのフレームの最初の一次ヒットからReservoirを作ります。後続サンプルで同じ三角形かつ近い位置へ当たる場合は再評価して共有し、それ以外は既存のNEE / MISへ戻します。
フレーム当たりの初期候補数はSPPにかかわらず8個です。ReSTIRの直接照明サンプル同士には相関があります。

## 推定値と二重計上

光源の面積測度で `f = BRDF × NoL × AO × emission × NoLight / distance²` を評価します。発光メッシュは両面発光のため光源側の余弦を絶対値にし、矩形光源は片面発光のため裏側の寄与を0にします。
IBLは立体角測度で `f = BRDF × NoL × AO × environmentRadiance × MIS_light` を評価し、既存のHDR行・列CDFから方向を生成します。
`MIS_light` は環境CDF PDFとBSDF PDFのpower heuristicです。環境PDFには、フレーム当たりの環境候補数の期待値（8 / 有効な光源種別数）をSPPで割った係数を掛け、BSDF側にも同じ係数を使います。これはMISの配分だけに使い、Reservoirの提案PDFや正規化には掛けません。IBLは光源側MISの積分をReservoirで推定し、相補的なBSDF側の推定を残します。HDRのフィルタで広がった強光源や狭い鏡面ローブをBSDF側でも扱えます。
有効な光源種別を均等な確率1/Nで選びます。提案PDFには種別の選択確率と、それぞれの面積／立体角PDFを含めます。矩形は `1 / (width × height)`、発光メッシュは三角形の選択確率を含む面積PDFです。
選択ターゲットは `max(luminance(f), 1e-5 × 提案PDF)`、初期重みは `target / 提案PDF` です。
Reservoirは光源種別を含むindex、光源内の位置または環境方向、生成時のIBL PDFを保持します。環境方向は方位角とY成分の2値で保存し、再利用時に復元します。32バイトのサイズは変更しません。
小さな正の下限により、黒いBRDFや反対向きの法線でも元の提案分布の台を失わず、別の受光点への再利用で単純な候補数正規化が使えます。可視性は選択ターゲットに含めません。
統合重みは `target_current × source.weight × source.count`、最終重みは `weightSum / (count × selectedTarget)` です。
時間履歴の有効候補数は32、空間入力は各40に制限し、制限後の候補数を統合重みと正規化の両方へ使います。

ReSTIRは一次頂点の有限BRDFによる、対象光源の照明を推定します。その次の非delta BSDFレイが、提案分布でサンプリング可能な発光面へ到達した場合、発光成分だけを0にして二重計上を避けます。
矩形光源も同様に次の非deltaレイの発光寄与を0にします。直接見える矩形・delta反射・ReSTIRを使わなかった一次頂点からの通常MISは維持します。最終評価は矩形上の位置を復元し、光源の手前までメッシュ・他の遮蔽物を再検査します。
IBLのMissでは従来どおり `MIS_bsdf` を使います。光源側との重みの和が1になるため二重計上せず、CDFの丸めなどでPDF = 0となる方向ではBSDF側の重みが1になります。delta反射も重み1です。
直接見える発光面、delta反射、発光面の通常のBRDF反射、CDFで確率0となる発光面へのBSDF寄与は維持します。
ReSTIRのReservoir重みを通常のBSDF PDFとみなして既存MISへ代入する処理は行いません。

参考: [ReSTIR原論文の公開ページ](https://research.nvidia.com/publication/2020-07_spatiotemporal-reservoir-resampling-real-time-ray-tracing-dynamic-direct)、[RTXDIの統合資料](https://github.com/NVIDIA-RTX/RTXDI/blob/main/Doc/Integration.md)。RTXDI自体の組み込みではありません。

## 検証

```powershell
.\tests\RunRestirDI.ps1 -Configuration Debug
.\tests\RunRestirDI.ps1 -Configuration Release
```

出力はGit管理外の `build/checks/restir-<構成>/` です。WhiteFurnaceTests・MeshLightTestsを先に実行します。
既存のWhiteFurnace 4,128条件とRR 14条件に加えて、Reservoirの32条件を各131,072サンプルで積分します。
色付きLambert・非金属・金属・吸収体、正面・斜め視線で、1候補・8候補RIS・2入力統合・5入力統合を比較します。
別の入力Reservoirは意図的に法線を反転し、ゼロのBRDFターゲットと候補数制限も検証します。
Lambertは有限矩形の解析解、他は1候補推定の平均と比較します。平均差の閾値は0.0005または0.001に6標準誤差を加えます。
Lambertの8候補RISは1候補の分散の1/5未満を要求します。再投影の既知座標・平行移動・背後判定、位置・法線・Material IDによる履歴棄却も検査し、`restir.csv` に結果を保存します。

画像はNEE・RIS・時間再利用・空間再利用の複数色照明（各1024サンプル）、影と鏡（512サンプル）、全消灯、G-buffer各表示（128サンプル）です。
画面右60%以降・下45%を2画素間隔で測り、RGB各平均値がNEEと±1以内か検査します。これは表示変換後の回帰確認で、線形の推定値はGPU数値テストで別に確認します。
法線表示の床、静止時のゼロモーション、前フレーム履歴が有効な画素も検査します。
NEE / MIS・発光三角形0でも法線・深度・モーションの3表示を検査します。
すべて終了コード0・PNG保存を確認し、保存画像も開いてください。`images.json` に統計と画像パスを記録します。

デバッグ表示は `--restir spatial --restir-debug normal|depth|motion|reservoir` です。Motionの中間灰色は0画素移動、Reservoir表示の緑は有効な前フレーム履歴、赤は候補数です。
法線・深度・モーションは `--restir-debug normal` のように単独でも使用できます。NEE / MISではReservoirは空で、RISでは時間履歴の緑表示は無効です。
手動のカメラ操作、移動中のちらつきや大きなカメラ移動後の収束、リサイズ・編集中のUI操作は別途確認が必要です。
GI / PTの経路再利用、透過、動的なメッシュ変形、デノイザーは対象外です。

## IBL専用の検証と時間計測

```powershell
.\tests\RunRestirEnvironment.ps1 -Configuration Debug
.\tests\RunRestirEnvironment.ps1 -Configuration Release
```

出力は `build/checks/restir-ibl-<構成>/` です。画像・時間の比較が干渉しないよう、このスクリプトは構成ごとに逐次実行してください。
既存の数値テストに加え、IBL単独／発光メッシュ併用、5種のマテリアル、2視線角、4サンプリング方式を各131,072サンプルで検査します。
通常の非一様CDFと、1セルだけに確率を持つCDFの各80条件、合計160条件です。
単位白色IBLを積分対象とし、提案分布だけを非一様にします。確率0領域のBSDF補完、方向の保存・復元、鏡のdelta寄与、混合PDF、異なる受光法線のReservoir統合を合わせて確認します。
数値テストの有限発光面と環境は独立した未遮蔽の積分対象で、併用時のLambert基準値はBase Color × (1 + 矩形の形態係数)です。実描画では両者の遮蔽も評価します。
結果は `restir-environment.csv` / `restir-environment-sparse.csv` へ保存します。

画像用に自作の64×32 HDR（背景約0.02、3×3画素の輝度32）と黒いHDRを生成します。
発光のないLambert平面でMIS・RIS・時間・空間再利用・BSDF単独を同じ512サンプルで比較し、MISの4096サンプル画像を基準にノイズを測ります。
吸収体の画像をマスクにして境界を2画素除外し、画面右側60%以降の平面内部を2画素間隔で測定します。
8bit PNGをsRGB・Reinhardの逆変換で線形放射輝度へ戻して赤チャンネルのRMSEを求めるため、量子化誤差と基準画像の有限サンプル誤差は含まれます。
ReSTIRとMISの表示変換後の赤平均値は±1以内を要求します。BSDF単独は比較値を記録しますが、この表示変換後の平均値の閾値は適用しません。
白い拡散面・粗面金属・鏡、全黒のHDR、発光メッシュとの併用、遮蔽物、75度の環境回転も画像で検査します。

`--gpu-timing <出力.csv>` はGPUタイムスタンプで照明ディスパッチ群の前後を測り、前フレームの完了後に読み戻します。
ReSTIRの準備・再利用とパストレーシングを含み、起動・モデル読み込み・ImGui・Present待ちは含みません。CSVは `gpu_ms` 列です。
検証スクリプトは先頭10測定フレームを除いて平均し、PNGの誤差・プロセス全体の所要時間とともに `images.json` に記録します。
これは固定シーン・固定SPPでの比較で、光源候補数は手法間で異なります。他のGPU処理やクロック状態の影響を受けるため、一般的な速度倍率を示すものではありません。
