# DLSS Ray Reconstruction

Streamline **2.14.16** のDLSS-RRをUsdKitchenへ組み込みます。NVIDIA RTX GPUと対応ドライバが必要です。ネイティブ解像度のDLAAモードを使い、アップスケーリング・Frame Generationは行いません。RRは既定でオフです。

## SDKの復元

```powershell
.\tools\RestoreStreamline.ps1
```

公式 [Streamline v2.14.16](https://github.com/NVIDIA-RTX/Streamline/releases/tag/v2.14.16) のZIPを取得し、固定SHA256を検証して `packages/Streamline-2.14.16/` に展開します。OSへのインストールは行いません。ビルドは自動ダウンロードしません。ヘッダーはUsdKitchenのビルドに必要ですが、実行DLLのない環境では従来の蓄積描画を使用できます。

DLLの利用前に、利用者自身で `packages/Streamline-2.14.16/license.txt` と `bin/x64/nvngx_dlss.license.txt` を確認し、必要な条件へ同意してください。StreamlineのMITライセンスと、独自条件のDLSSバイナリのライセンスは別です。配布時の条件は原文を確認してください。

同意後、次を実行すると、Git管理外の `build_support/Streamline.local.props` に実行DLLのコピー設定を保存します。

```powershell
.\tools\RestoreStreamline.ps1 -AcceptNvidiaLicense
```

Visual Studio 2026で `PathTracingSample.slnx` をビルドすると、UsdKitchenの出力先の `Streamline/` に署名付きDLL4個とライセンスをコピーします。SDKやDLLをリポジトリへコミットしません。スクリプトを同意指定なしで再実行しても既存の設定は維持します。

## 実行と比較

```powershell
.\build\x64\Release\UsdKitchen\UsdKitchen.exe --rr
.\build\x64\Release\UsdKitchen\UsdKitchen.exe --no-rr
.\build\x64\Release\UsdKitchen\UsdKitchen.exe --rr --restir spatial --samples 128 --spp 1 --screenshot build/checks/rr.png
```

UIの `DLSS Ray Reconstruction` から切り替えられます。非対応GPUやDLL未配置ではチェックを無効にし、理由を表示します。明示的な `--rr` が利用できない場合はエラー終了します。`--no-rr` は従来のHDR蓄積を使用します。

RRには各フレームの未蓄積HDRを渡します。`--samples 128 --spp 1` は128フレーム分の履歴を待つ指定で、128 sppの入力を毎フレーム渡す指定ではありません。RR有効時のGPU時間はガイド生成とRR評価も含みます。

ImGuiを除いた表示領域専用のテクスチャを使い、RR後にトーンマッピングとImGui合成を行います。Haltonジッターと、ジッターを除いたcurrent→previousのピクセル単位モーション、正のビュー空間深度、ワールド空間法線とperceptual roughness、拡散／鏡面の積分反射率、一次面からの鏡面レイのヒット距離を渡します。反射距離用に独立したGGXレイを1本追加します。有限ヒットがない距離は0とします。

カメラ移動ではRR履歴を維持し、モデル・マテリアル・照明設定、表示領域、解像度の変更やReset accumulationでは履歴をリセットします。現在は静的メッシュのみで、物体の変形・移動ベクトルは扱いません。ReSTIRのG-buffer表示とMeshlet colorsはRRを迂回します。

`RR view` または `--rr-view output|noisy|normal|roughness|diffuse|specular|motion|depth|hit-distance` で入力を可視化できます。このCLI指定はRRも有効にします。Motionは0を中間灰色、10 pixelを半レンジで表示し、深度／距離は `d / (1 + d)`、アルベドはsRGB変換して表示します。

NormalからSpecular hit distanceまでの診断表示は、揺れを避けるためピクセル中心のレイで生成します。反射距離用レイの乱数もフレーム間で固定し、RRの評価と不要な多重バウンスは実行しません。通常のOutputとNoisy HDRは従来どおりジッターを使います。Noisy HDRは未蓄積の入力であり、サンプリングのノイズや輪郭のジッターが見えます。表示をOutputへ戻すとRR履歴をリセットします。Motion用の前フレームのカメラ情報は診断表示でも更新します。

`Scene GPU` はシーン描画・ガイド生成・OutputでのRR評価のGPU時間です。VSync待機、ImGui描画、CPU側処理やPNG保存の時間を含まないため、画面のFPSとは区別してください。PresentはVSync有効で、RRによる負荷増加でリフレッシュ周期を超えるとFPSが下がる場合があります。RRを30 FPSへ固定する処理はありません。

## 確認

Debug / Releaseをビルド後、それぞれ実行します。

```powershell
.\tests\RunRayReconstruction.ps1 -Configuration Debug
.\tests\RunRayReconstruction.ps1 -Configuration Release
```

自作Preview Roomのオン／オフ、ReSTIR併用、各ガイド、発光メッシュ、白色環境の鏡・粗い金属・拡散・吸収体、および存在すればローカルKitchen Setを撮影します。終了コード、SDKのエラーログ、黒画面、白色環境の平均値とRMSEを検査し、PNGとログを `build/checks/ray-reconstruction-<構成>/` に保存します。白色の基準値は8bit sRGBで188、RR用の許容差は平均2／RMSE8です。これはデノイザを通した静止画のスモークテストで、BRDF自体の正当性は既存WhiteFurnaceTestsが検証します。

PNGは必ず開いて表示も確認してください。この自動撮影だけではカメラ移動中の残像や、手動リサイズ・切り替えの応答は検証できません。これらは別に確認し、未実施なら報告に明記します。SDKの評価APIが成功を返しても内部エラーを記録する場合があるため、評価中のエラーログもアプリ側で検出して終了します。

静止時の揺れは、同スクリプトから呼ぶ `RunRayReconstructionStability.ps1` で検証します。128フレーム待った後に16フレーム連射し、白色環境内の黒い平面の輝度で重み付けした重心を測ります。縦横それぞれの最大値－最小値が0.15 pixel以下であることを確認し、`build/checks/rr-stability-<構成>/` にPNGとstability.jsonを保存します。カメラ移動中の品質を保証するテストではありません。

同じスクリプトから呼ぶ `RunRayReconstructionGuideStability.ps1` は、Normal・Roughness・拡散／鏡面アルベド・Motion・Depth・反射距離それぞれを16フレーム連射し、ImGuiを除く領域のRGBが完全一致することを検証します。ローカルKitchen SetがあればそのNormalも確認し、SDKログで診断表示中にRRを評価していないことを検査します。結果は `build/checks/rr-guides-<構成>/` に保存します。

レイが `pixelCenter + sampleJitter` をサンプルする場合、入力画像上の形状は `-sampleJitter` 移動するため、Streamlineへ渡す `jitterOffset` はサンプルオフセットの逆符号です。投影関連の行列はジッターを含めず、モーションもジッターを除いた値を維持します。ジッター自体を無効にして揺れを隠す処理は行いません。

本実装はNVIDIA提供のStreamlineヘッダーと統合例を使用します。反射率近似はSDKのDLSS-RRガイドにあるEnvBRDFApprox2を基にしています。[MITの通知](../third_party/Streamline-LICENSE.txt) を参照してください。
