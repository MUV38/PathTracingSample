# USD Kitchenサンプル

`UsdKitchen` は、別プロジェクトでUSDから変換した `.ptscene` を読み、単一BLAS / TLASでDXR描画します。
OpenUSDのヘッダー、DLL、Pythonは表示アプリに不要です。ルートの `.slnx` はUSD SDKなしでビルドできます。
既存のWASDフリーカメラ、Alt回転・パン・ドリー、ホイールによる速度変更、撮影機能を利用できます。
F / Frame modelはシーン全体にカメラを合わせます。HeadlightとExposureで表示を調整できます。

初期表示はリポジトリ用に作成した **Preview Room** です。Kitchen Setとは異なる簡易モデルです。
ソースは `assets/Source/PreviewRoom/`、変換済みデータは `assets/PreviewRoom.ptscene` です。

## Kitchen Setを表示する

1. [公式ダウンロード一覧](https://openusd.org/release/dl_downloads.html) のKitchen Setを開き、利用者自身で条件を確認してダウンロードします。
2. ZIPを `assets/local/KitchenSet/` などへ展開します。インストール操作は不要です。参照先のUSDやテクスチャを含むフォルダ構造を維持してください。
3. [UsdConverter](../../tools/UsdConverter/README.md) を別ソリューションでビルドします。
4. 実際に展開された `Kitchen_set.usd` のパスを指定し、変換します。

```powershell
.\build\x64\Release\UsdConverter\UsdConverter.exe assets/local/KitchenSet/Kitchen_set.usd assets/local/KitchenSet/Kitchen.ptscene
.\build\x64\Release\UsdKitchen\UsdKitchen.exe --model assets/local/KitchenSet/Kitchen.ptscene
```

相対パスはカレントディレクトリ基準です。空白を含むパスは引用符で囲んでください。
Kitchen Setは同梱・自動取得しません。公式の利用条件に従い、入手データおよび変換したデータもコミットしないでください。
`assets/local/` と `build/` はGit管理外です。Kitchen Set実データでの変換・表示確認は未実施です。

## アプリから変換する

`Open USD...` でソースを選び、必要なら `Choose converter...` でビルド済み `UsdConverter.exe` を選び、`Convert and load` を押します。
変換は独立プロセスで実行します。既定では同じ構成の隣のUsdConverter、次にRelease構成を探します。
変換中も描画でき、キャンセル可能です。失敗時は現在のモデルを維持し、Conversion logにエラーを表示します。
変換結果とログは実行ファイル隣の `cache/` に保存します。必要に応じてアプリ終了後に削除してください。
`Open binary...` から既存バイナリも開けます。

```powershell
.\build\x64\Debug\UsdKitchen\UsdKitchen.exe --usd assets/Source/PreviewRoom/PreviewRoom.usda --converter "build/x64/Release/UsdConverter/UsdConverter.exe"
```

自動撮影では変換・ロードが終わってから撮影します。起動時の変換失敗は終了コード1と標準エラーで通知します。

```powershell
$app = Start-Process .\build\x64\Release\UsdKitchen\UsdKitchen.exe -ArgumentList '--usd "assets/Source/PreviewRoom/PreviewRoom.usda" --screenshot "build/checks/usd.png"' -Wait -PassThru
$app.ExitCode
```

## 対応範囲

静的メッシュの形状確認用です。階層変換、参照・ペイロード、ネイティブインスタンス、凹多角形、法線、displayColor、定数のマテリアル色を変換します。
ステージ開始時刻をメートル単位、右手系Y-upの三角形に固定します。
サブディビジョンは制御ポリゴンのままです。テクスチャ、USDライト、透明度、シェーダーネットワーク、アニメーション、PointInstancerは未対応です。
色と簡単な拡散照明によるプレビューであり、USDの最終ルックを再現するものではありません。
Path Tracing、影、多重反射、CLASは今回の実装には含みません。
