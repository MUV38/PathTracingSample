# ReSTIR DI（発光メッシュ）

一次ヒットの発光メッシュ照明に、RISと時間・空間Reservoir再利用を追加します。外部SDKやライブラリは追加しません。
既定は従来のNEE / MISです。ImGuiの `ReSTIR DI` から選ぶか、CLIで切り替えます。

```powershell
.\build\x64\Release\UsdKitchen\UsdKitchen.exe --model build/checks/restir-Release/multiple.ptscene --restir spatial --samples 1024 --spp 4
```

モデルは下記の検証スクリプトで生成します。Kitchen Setの原本は使用しません。

| `--restir` | 処理 |
|---|---|
| `off` | 既存の発光メッシュNEE / MIS |
| `ris` | 面積×輝度CDFから8候補を生成し、1個のReservoirへ集約 |
| `temporal` | RISに前フレームのReservoirを追加 |
| `spatial` | 時間再利用後、半径8画素内から最大4近傍も統合 |

IBL、矩形ライト、方向光・点光源、二次ヒット以降のNEE / MISは従来の方式です。`--no-emissive-mis` では発光メッシュNEEとReSTIR照明を無効にします。
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

光源の面積測度で `f = BRDF × NoL × AO × emission × |NoLight| / distance²` を評価します。
選択ターゲットは `max(luminance(f), 1e-5 × 面積PDF)`、初期重みは `target / 面積PDF` です。
小さな正の下限により、黒いBRDFや反対向きの法線でも元の提案分布の台を失わず、別の受光点への再利用で単純な候補数正規化が使えます。可視性は選択ターゲットに含めません。
統合重みは `target_current × source.weight × source.count`、最終重みは `weightSum / (count × selectedTarget)` です。
時間履歴の有効候補数は32、空間入力は各40に制限し、制限後の候補数を統合重みと正規化の両方へ使います。

ReSTIRは一次頂点の有限BRDFによる発光メッシュ照明全体を推定します。その次の非delta BSDFレイが、提案分布でサンプリング可能な発光面へ到達した場合、発光成分だけを0にして二重計上を避けます。
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
