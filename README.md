# ViewReservation

TVTest 用の視聴予約 / 録画予約プラグインです。
EPG 番組表から登録した予約に従って、指定時刻に自動でチャンネルを切り替えます。

## 特徴

- EPG 右クリックから視聴予約 / 録画予約を登録
- 開始時刻の30秒前に自動切替
- 放送中の番組は登録後すぐに切替
- 地上波 / BS / CS の BonDriver 跨ぎ切替
- 1つの予約につき切替は1回のみ
- UI なし / 通知なし / サイレント動作
- x64 / x86 両対応

## 推奨構成

- 視聴用: `TVTest_View.exe` + ViewReservation
- 録画用: `TVTest_Record.exe` + 既存の録画環境

この構成にすると、録画と視聴の干渉を避けやすくなります。

## 動作要件

- Windows 10 / 11
- TVTest 0.10.0 以降
- BonDriver ベースのチューナー環境
- Visual Studio 2022（ソースからビルドする場合）

## インストール

1. Release から ZIP をダウンロードします。
2. 使用している TVTest の種類に合わせて `x64` または `x86` の `ViewReservation.tvtp` を TVTest の `Plugins` フォルダへ配置します。
3. TVTest を起動します。

## ソースからビルド

1. `ViewReservation.sln` を Visual Studio 2022 で開きます。
2. `Release | x64` または `Release | Win32` を選択します。
3. ビルドします。
4. 出力された `ViewReservation.tvtp` を TVTest の `Plugins` フォルダへ配置します。

## 使い方

1. TVTest の EPG 番組表を開きます。
2. 番組を右クリックします。
3. `視聴予約` または `録画予約` を選択します。
4. 予約時刻になると自動でチャンネル切替が行われます。

## 設定ファイル

予約情報は `ViewReservation.ini` に保存されます。

## ドキュメント

- `docs/Documentation_JP.txt`

## ライセンス

このリポジトリ内の自作コードは MIT License です。
TVTest 本体、BonDriver、B25 関連ファイルは同梱していません。

## ステータス

現在の公開版: v1.8.0
