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
入手したKitchen Setの原本と変換データは `assets/local/` または `build/` に置き、コミットしないこと。同梱のPreview Roomは自作モデルでありKitchen Setと混同しないこと
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
