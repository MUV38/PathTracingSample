# GGXエネルギーLUT

実描画のheight-correlated Smith GGXに合わせて、Fresnel = 1の単一散乱によるエネルギー損失 `L(NoV, roughness) = 1 - E` を積分します。
外部ライブラリは使いません。生成済みの `samples/UsdKitchen/GgxEnergyLut.h` をコミットしているため、通常のビルドや起動時の生成は不要です。

Visual Studio 2026のDeveloper PowerShellでリポジトリ直下から再生成できます。

```powershell
MSBuild tools/GgxEnergyLut/GgxEnergyLut.vcxproj /p:Configuration=Release /p:Platform=x64
.\build\x64\Release\GgxEnergyLut\GgxEnergyLut.exe samples/UsdKitchen/GgxEnergyLut.h
```

65×65の端点を含む格子で、横軸は `sqrt(NoV)`、縦軸はperceptual roughnessです。ゼロ以外の粗さは実描画と同じ0.02下限・alpha = roughness²を使います。
各ノードを倍精度・65,536個のHammersley VNDFサンプルで積分します。面の下に出た反射は寄与0です。
Fresnel = 1におけるサンプル重みは `G2(V,L) / G1(V)`。GGXのDや反射ヤコビアンを直接除算しない形で計算します。
NoV = 0の極限と粗さ0はエネルギー損失0です。正面視では接平面の向きを自由に選べます。

格子の4,225値に続けて、各行の `Lavg = 2 ∫ L(mu) mu dmu` を65値格納します。
平均値は実行時と同じ補間関数を16,384点の中点則で積分します。粗さ方向の線形補間後も平均値との関係を維持します。
GPUには合計17,160バイトの読み取り専用StructuredBuffer（t9）として渡し、手動で双線形補間します。

補償はKulla–Contyの相反性を持つ追加ローブです。

```text
Favg = F0 + (F90 - F0) / 21
Fms = Favg² (1 - Lavg) / (1 - Favg Lavg)
f_ms(V,L) = Fms L(V) L(L) / (pi Lavg)
```

F90は既存のSchlickと同じ定義です。Lavgが1e-6以下の場合は小さな補償項を省略し、完全鏡のdelta成分には追加しません。
補償ローブと拡散ローブを余弦半球、単一散乱をVNDFで混合サンプリングし、その混合PDFをすべての光源のMISで使います。
実際の微小面上のランダムウォークを追跡する方式ではなく、等方性・反射のみの近似です。非金属の拡散層を含めた厳密な多層輸送は扱いません。

参考: [Kulla–Contyの補償式（Filament）](https://google.github.io/filament/Filament.md.html#materialsystem/specularbrdf/energycompensation)、[Heitz 2018 VNDF](https://jcgt.org/published/0007/04/01/paper.pdf)。

再生成やGGXの変更後はDebug / Releaseの [RunWhiteFurnace](../../tests/WhiteFurnace.md) を実行してください。
CPUの生成コードとは独立に、実描画HLSLによる白い金属の反射率、色付き金属・非金属の上限、相反性、独立の余弦積分、全光源のMISを検証します。
