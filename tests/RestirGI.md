# 拡散GIの初期候補

この段階は **候補生成と再利用なしの再構成** です。時間／空間再利用やノイズ低減はまだ行いません。
外部SDKは追加せず、既存のPT経路、BRDF、NEE/MIS、Russian Rouletteを利用します。

## 起動と診断表示

```powershell
.\build\x64\Debug\UsdKitchen\UsdKitchen.exe --model assets/PreviewRoom.ptscene --restir-gi candidates
.\build\x64\Debug\UsdKitchen\UsdKitchen.exe --model assets/PreviewRoom.ptscene --gi-view diffuse --samples 512 --screenshot build/checks/gi-diffuse.png
.\tests\RunRestirGI.ps1 -Configuration Debug
.\tests\RunRestirGI.ps1 -Configuration Release
```

ImGuiの `ReSTIR GI / initial candidates` で生成と表示を切り替えます。
`--restir-gi off|candidates` の既定はoffです。`--gi-view` は次の値を受け取ります。
Output以外のGI表示は、生成チェックの状態にかかわらず候補生成を有効にし、RRを迂回します。

| 値 | 表示 |
| --- | --- |
| output | 通常の総照明。候補生成有効時は拡散間接光を候補の評価値に置き換える |
| diffuse | 候補が担当する拡散間接光のみ |
| remainder | 総照明からその拡散間接光を除いた値。直接光・鏡面・対象外の面など |
| radiance | 候補の二次面から一次面へ返る反射放射輝度。一次面のBRDF重みを含まない |
| normal | 二次面の幾何法線。無効候補は黒 |
| pdf | 赤：立体角PDF、緑：面積PDF。各成分をp/(1+p)で表示 |
| valid | 有効候補は緑、無効候補は黒 |
| error | 元の経路から分離した拡散GIと候補再評価の絶対差×1000。通常はほぼ黒 |

照明と放射輝度の表示は既存の蓄積・露出・トーンマッピングを使用します。
normal / pdf / validは、診断表示自体が揺れないようピクセル中心と固定乱数を使用し、蓄積せず表示します。
Outputは既存のPT乱数を維持し、RRを有効にした場合は既存のジッターを使用します。
DIのG-buffer表示を同時に選ぶとDI表示を優先します。Meshlet colorsではGIを無効にします。

## 経路の分離

一次面のroughnessが0.2以上で、拡散項が正の有限BRDFサンプルを対象にします。
全金属、鏡、低roughnessの一次面は従来のPTを使用します。GGX多重散乱補償も従来側に残します。
拡散枝だけを新たに選び直すのではなく、既存のGGX VNDF＋cosine混合で生成した方向と、**混合全体の立体角PDF**を保存します。
候補生成で追加の乱数やレイは消費しません。

二次面へ当たった時点で位置・幾何法線を保存し、そこから先の反射放射輝度を別のthroughputで追跡します。
二次面自身の発光、一次面から直接見えた矩形光源とIBLは直接光なので候補に含めません。
これらは元のPT側で既存MISを維持します。二次面の直接照明・NEE、さらに先の反射・発光・IBLは候補の放射輝度に含めます。
Russian Rouletteの乱数・生存判定は元のPTと共有し、候補側のthroughputにも同じ生存確率の補正を適用します。

候補の評価は `f_diffuse × cos(theta) × occlusion × Li / p_omega` です。
元のPTから分離した同じ拡散間接成分を取り除いてから、この評価値を加えます。
そのためこの段階では候補有効／無効で総照明が一致し、拡散間接光を二重加算しません。
面積PDFも `p_A = p_omega × abs(dot(N_secondary, -direction)) / distance²` で保存しますが、
現在の評価は立体角measureなので面積・距離の係数を再び掛けません。
現状のレイ原点バイアスに伴う微小な幾何差は既存PTと同じ扱いです。

## 保存と制限

GPUの `GiCandidate` は96バイトです。二次位置・法線、立体角／面積PDF、反射放射輝度、
一次位置・三角形番号、拡散BRDF×cos×occlusion、逆PDF、方向、立体角measureのtarget densityを保存します。
生成有効時だけ全画面分を確保し、無効時は1要素です。1280×720では約84.4MiB、1920×1080では約189.8MiBです。

全sppの候補をその場で評価してフレームを平均します。GPUバッファに残すのは各画素の**最初のサンプルだけ**です。
照明経路では毎フレーム全画素を上書きし、Miss、対象外、bounce上限0、パネル領域には無効候補を書きます。履歴バッファはありません。
RRのガイド専用表示ではGI候補を生成・参照しません。照明表示に戻ったフレームで新しい候補へ上書きします。
候補が黒でも、サンプリングされた有効な二次面であればvalidは1です。

二次面は現時点では既存PBRをそのまま評価するため、放射輝度に方向依存性があります。
これをそのまま別画素・別フレームへ使い回す実装にはしていません。時間再利用を追加するときは、
再投影・候補数と重みの正規化・再接続のJacobian・可視性・secondary BRDFの扱い・遮蔽変化の棄却が必要です。
この段階では追加メモリと計算が増えるだけで、性能向上やノイズ低減は期待しないでください。

## 自動検証

RunRestirGIはWhiteFurnaceTests内のGPU数値チェック（PDFのmeasure変換、単一候補の重み、対象外マテリアル）と
既存のMeshLightTestsを実行し、17ケースを自動撮影します。
通常PT、ReSTIR DI併用、RR併用、発光＋6バウンスで、候補有無の同一乱数画像を比較します。
8bit画像の平均絶対差0.2以下・最大差4以下を許容し、診断の非空、全金属とbounce上限0での無効化、
再構成誤差表示の平均値1以下も検査します。領域は右側35%、4画素間隔です。
diffuse＋remainderもsRGBとトーンマッピングを逆変換し、8bit量子化区間内で総照明と一致することを検査します。
これは照明画像のスモークテストであり、完全な数値誤差や未検出NaNの不存在を保証する検査ではありません。

PNG・stderr・summary.csvは `build/checks/restir-gi-<構成>/` に保存します。終了コードだけでなくPNGも開いて確認してください。
カメラの手動操作や動く物体、時間／空間再利用はこのテストの対象外です。

実装時には両構成で上記テストとRunWhiteFurnace / RunRussianRoulette / RunRayReconstructionを実行し、保存画像も確認しました。
通常PT／DI／RR／発光シーンの候補有無は検査領域でMAE=0、最大差0でした。
追加でローカルKitchen SetのGI有効描画と、GI normalの16フレーム連射も両構成で確認しました。
GI normalは右側35%を8画素間隔で比較し、変化画素0でした。手動のUI・カメラ操作は未確認です。

初期候補と再利用なしの比較という段階分けは [NVIDIA RTXDIのReSTIR GI説明](https://github.com/NVIDIA-RTX/RTXDI/blob/main/Doc/RestirGI.md) も参照しています。RTXDI SDK自体の追加・組み込みは行っていません。
