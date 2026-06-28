# Contributing

バグ報告や改善提案を歓迎します。

## バグ報告時に欲しい情報

- ViewReservation のバージョン
- TVTest のバージョン
- x64 / x86 のどちらを使用しているか
- 使用 BonDriver 名
- 使用チューナー
- 問題が起きた時刻
- 再現手順
- `ViewReservation.ini` の内容
- TVTest のログ全文

## Pull Request

- Visual Studio 2022 で開ける状態を維持してください。
- x64 / x86 の両構成を壊さないでください。
- 同じ意味の処理を別ルートで増やさないでください。
- 変更理由と影響範囲を説明してください。

## 開発方針

- UI を増やさない
- 通知を増やさない
- 視聴予約 / 録画予約の切替を静かに確実に行う
- TVTest 本体、BonDriver、B25 関連ファイルを同梱しない
