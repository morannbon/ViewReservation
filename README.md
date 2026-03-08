# ViewReservation

TVTest 用の **視聴予約専用プラグイン** です。  
EPG 番組表から登録した予約に従って、指定時刻に自動でチャンネルを切り替えます。

録画機能は持たず、**TvRock などの録画ソフトと併用**する前提です。

## 特徴

- EPG 右クリックから視聴予約
- **開始時刻の30秒前**に自動切替
- 放送中の番組は TVTest 起動時に即時切替
- 地上波 / BS / CS の **BonDriver 跨ぎ切替**
- **1つの予約につき切替は1回のみ**
- UI なし / 通知なし / サイレント動作
- TvRock の録画用 TVTest とは分離運用を想定

## 推奨構成

- 視聴用: `TVTest_View.exe` + ViewReservation
- 録画用: `TVTest_Record.exe` + TvRock

この構成にすると、録画と視聴の干渉を避けやすくなります。

## 動作要件

- Windows 10 / 11
- TVTest 0.10.x
- BonDriver ベースのチューナー環境
- Visual Studio 2022（ソースからビルドする場合）

## インストール

1. `source` 内のプロジェクトを VS2022 でビルド
2. 出力された `ViewReservation.tvtp` を TVTest の `Plugins` フォルダへ配置
3. TVTest を起動

## 使い方

1. TVTest の EPG 番組表を開く
2. 番組を右クリック
3. **視聴予約** を選択
4. 予約時刻になると自動でチャンネル切替

## 設定ファイル

予約情報は `ViewReservation.ini` に保存されます。

## ライセンス

このリポジトリ内の自作コードは **MIT License** です。  
TVTest 本体、BonDriver、B25 関連ファイルは同梱していません。

## ドキュメント

- 日本語配布ドキュメント: `docs/Documentation_JP.pdf`

## ステータス

長期安定運用向け最終版: **v1.7.1 UltraStable**