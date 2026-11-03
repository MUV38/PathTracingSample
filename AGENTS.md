# 作業要件
外部ソフトウェアを勝手にインストールして組み込まない
過剰な継承や抽象化は避け、可読性を重視したシンプルな設計を目指すこと
作業が終わったら、最新のmainでrebaseを行い、pull requestの作成まで行うこと
ただし、AIが作成したpull requestがすでに存在するときは作業内容に応じて既存のpull requestにpushしてもよい
if文で1行で終わる実行コードも{}で囲い、それぞれ改行するようにする
基本的にインデントはタブを使用する、コードを整形するために必要なスペースは許可する

# 描画・UIの確認
描画やUIを変更した場合は、Debug / Releaseのビルドに加えて実行結果の画像も確認すること
外部の画面取得ツールが失敗する場合は、アプリ内のスクリーンショット機能を使用すること
自動確認には各サンプルの `<sample.exe> --screenshot <保存先.png>` を使用する。3フレーム描画後、ImGuiを含むPNGを保存して終了する
相対パスはカレントディレクトリ基準。確認用画像はGit管理対象外の `build/checks/` 配下に保存すること
自動撮影の終了コードが0であることに加え、保存されたPNGを開いて表示内容を確認すること。終了コード1の場合は標準エラー出力も確認すること
手動撮影はImGuiの `Save screenshot (F12)` ボタンまたはF12キーを使用する。実行ファイル隣の `screenshots/` に保存される
スクリーンショットによる表示確認と、入力・操作の動作確認は区別し、未確認の項目は作業報告に明記すること
詳しい実行例はREADME.mdの「スクリーンショット」を参照すること
UsdKitchenの間接照明・IBLを確認するときは `--samples 128 --screenshot <保存先.png>` などで蓄積を待つこと。既定の数フレーム撮影だけで収束を評価しないこと
Russian Rouletteの継続確率・重み・適用位置を変更した場合はDebug / Releaseの `tests/RunRussianRoulette.ps1 -Configuration <構成>` を実行し、RR有無のPNGを開いて確認すること。検証範囲は `tests/RussianRoulette.md` を参照すること
ReSTIR DIのReservoir・G-buffer・再投影・履歴・発光の二重計上処理を変更した場合はDebug / Releaseの `tests/RunRestirDI.ps1 -Configuration <構成>` を実行し、NEEとの平均値・モーション・履歴表示と保存PNGを確認すること。手順と制限は `tests/RestirDI.md` を参照すること
ReSTIR DIのIBL・光源種別の混合・Miss寄与を変更した場合は `tests/RunRestirEnvironment.ps1 -Configuration <構成>` もDebug / Releaseで実行し、通常CDFと確率0を含むCDF、既存IBL MISとの画像・平均値・GPU時間の比較を確認すること。時間計測時は構成を並列実行しないこと
IBLのHDR読み込みを変更した場合はDebug / Releaseの `EnvironmentMapTests.exe build/checks` を実行すること
IBLのサンプリング分布・NEE・MISを変更した場合もEnvironmentMapTestsとRunWhiteFurnaceを両構成で実行し、`--no-ibl-mis` と同じサンプル数で画像を比較すること
UsdKitchenのBRDF評価・サンプリングを変更した場合はDebug / Releaseの `tests/RunWhiteFurnace.ps1 -Configuration <構成>` を実行し、出力PNGも確認すること。判定範囲と許容誤差は `tests/WhiteFurnace.md` を参照すること
GGXのD・Smith visibility・粗さの定義を変更した場合は `tools/GgxEnergyLut/README.md` の手順でエネルギーLUTも再生成すること。補償あり／なしの白い粗面金属・色付き金属・非金属をWhiteFurnaceで検証すること
矩形面光源の交差判定・PDF・NEE/MISを変更した場合はDebug / Releaseの `tests/RunRectangleLight.ps1 -Configuration <構成>` を実行すること。保存PNGを開き、大小ライトの半影、表裏、鏡への映り込みも確認すること。手順は `tests/RectangleLight.md` を参照すること
発光メッシュの選択分布・PDF・NEE/MISを変更した場合はDebug / Releaseの `MeshLightTests.exe <出力先>`、`tests/RunEmissiveLights.ps1 -Configuration <構成>`、WhiteFurnaceTestsを実行し、複数色の照明・影・鏡の保存画像も確認すること。手順は `tests/EmissiveLights.md` を参照すること

# サンプル構成
ウィンドウ、DirectX 12初期化、ImGui、撮影機能は `common/` のSampleFrameworkで共有する
サンプル固有の描画とシェーダーは `samples/<サンプル名>/` に置き、共通Rendererへサンプル別の分岐を追加しないこと
共通のVisual Studio設定は `build_support/Cpp.props` と `Cpp.targets` に集約する
実行ファイルは `build/x64/<構成>/<プロジェクト名>/` に出力する。サンプル追加手順は `samples/README.md` を参照すること
カメラの計算は `common/Camera` を再利用し、Win32 / ImGuiの入力振り分けはサンプル側で行うこと
カメラの計算を変更した場合は、Debug / Releaseの `CameraTests.exe` を実行すること

# USDデータ
OpenUSD SDKは `tools/UsdConverter/UsdConverter.slnx` の独立した変換プロジェクトにのみリンクすること
表示サンプルは `common/SceneFile` の変換済み `.ptscene` を読み、USD SDKやPythonに依存させないこと
`.ptscene` はPBRマテリアルを含むmeshlet専用v3形式とし、v1 / v2互換処理は追加しないこと。meshoptimizerは外部変換ツールとテストだけで使用し、表示時の再分割は行わないこと
入手したKitchen Setの原本と変換データは `assets/local/` または `build/` に置き、コミットしないこと。同梱のPreview Roomは自作モデルでありKitchen Setと混同しないこと
利用者が `assets/Source/Kitchen_set/` に配置した原本はその場所で読み、移動しないこと。このフォルダもGit管理対象外とする。通常利用のKitchen Setは `assets/local/Kitchen_set.ptscene` に保存し、初期表示でPreview Roomより優先すること。変換テストの結果・ログ・画像は `build/checks/` に保存してよい。実データの変換確認は `tests/RunKitchenSet.ps1 -Configuration <構成>` を使用すること
独自形式を変更した場合はバージョン互換性を明示し、Debug / ReleaseのSceneFileTestsを実行すること
変換ツールを変更した場合は自作USDフィクスチャを変換してSceneFileTestsの追加引数で検証すること。手順はtools/UsdConverter/README.mdを参照すること

# C++仕様
C++20を採用
命名規則は指定のないものは一般的なC++規則に従う
メンバ変数名はmHogeの形とする
関数名はcamelCaseとする
クラス、構造体名はPascalCaseとする

# hlsl仕様
命名規則は指定のないものは一般的なhlsl規則に従う
インクルードファイルにはhlsliを使用する
エントリーポイント名はCS, VS, PS, MS, ASのようにシェーダーステージの略称をつけること
