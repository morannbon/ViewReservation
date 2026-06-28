ViewReservation v1.8.0
=======================

Visual Studio 2022 用の TVTest プラグインプロジェクトです。

対応構成
--------
- Release | x64
- Release | Win32
- Debug | x64
- Debug | Win32

出力
----
- bin\x64\Release\ViewReservation.tvtp
- bin\Win32\Release\ViewReservation.tvtp

主な動作
--------
- EPG 右クリックから視聴予約 / 録画予約を登録
- 開始30秒前に自動チャンネル切替
- 放送中予約は登録後すぐに切替
- BonDriver 跨ぎ切替
- 予約ごとの切替は1回のみ
- UIなし / 通知なし / サイレント動作

v1.8.0 整理内容
----------------
- x86 / x64 の VS2022 構成を追加
- EPG 同期 / 再探索の実行間隔を予約管理側に保持
- 切替済み状態を INI に保存
- 予約保存処理の重複呼び出しを削除
- INI 書き込み処理を共通化
- 古い版名を含む重複ドキュメントを削除
