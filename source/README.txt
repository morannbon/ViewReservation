ViewReservation (rescue-logic optimized build)
==================================================

This archive is intended for Visual Studio 2022 (v143 / x64).
It uses the uploaded official TVTest SDK header TVTestPlugin.h ver.0.0.15-pre.

Main behavior
-------------
- EPG right click -> reserve / unreserve
- Auto switch 30 seconds before start
- Immediate switch if reserved while already on-air
- INI persistence
- Max 10 reservations
- Cross-BonDriver channel switching
- Rescue logic for BS/CS reorganization or service moves

CPU-load reductions in this revision
------------------------------------
1. The main monitor loop runs on a single 5-second wait rather than a 100 ms polling loop.
2. EPG direct sync is throttled by distance to airtime:
   - more than 6 hours away: every 10 minutes
   - more than 1 hour away: every 60 seconds
   - more than 10 minutes away: every 15 seconds
   - within 10 minutes: every 5 seconds
3. Expensive rescue scans are not run every tick.
   They are delayed / throttled and only run when direct lookup fails.
4. BonDriver / tuning-space enumeration is cached for 15 minutes and rebuilt only when needed.
5. Normal checks do not write to disk; INI is updated only on meaningful changes.

Rescue logic outline
--------------------
When the original NetworkID / TSID / ServiceID / EventID no longer resolves:
1. Try direct EPG lookup by current identity.
2. Search the same service event list by title / time / duration score.
3. Search other cached services (same network first, then full cache fallback).
4. If a strong candidate is found, update reservation identity automatically.
5. If nothing is found and airtime is still far away, keep the reservation and retry later.
6. If airtime is near and nothing is found, remove the reservation and log it.

Notes
-----
- This project is written to be buildable with the provided TVTest SDK header,
  but final runtime behavior can still depend on the actual BonDriver implementation.
- INI now stores original IDs and current IDs separately for rescue tracking.


[Ultra Low Load edition]
- 予約0件時は最長5分スリープ
- 次に必要な処理時刻まで可変スリープ
- 24時間以上先の予約は EPG同期を 30分間隔に抑制
- 救済探索は直接一致失敗時のみ実行
- BonDriver/チューニング空間キャッシュは30分保持




Final package note: This archive is intended as the ultra low-load final package as of 2026-03-07.





v1.1 安定版の追加修正:

- 同一予約の重複登録を抑止

- 壊れた INI の Count=0 / 重複セクションを起動時に自己修復

- 同一開始時刻かつ同一番組名の予約を統合

- 保存時に Count と実予約数を必ず一致させる





v1.2 修正点:

- 起動直後5分は EPG 未準備による予約解除を行わず、EPG再探索待ちにする

- 起動直後に近い開始時刻の予約が即解除される問題を軽減





v1.3 fixed 修正点:

- SaveUnlocked を const のまま実装修正

- 保存前に旧 Reservation セクションを個別削除

- Count とセクション数の不一致を抑制





v1.4 修正点:

- 起動後5分は開始30分以内/放送中予約を解除しない

- 切替対象を StartTime-30秒 から終了時刻までの予約に限定

- 切替ログに NID/TSID/SID を追加

- 衛星・地上波跨ぎ時の対象選択の誤爆を減らす方向で調整





v1.5 修正点:

- 起動直後90秒は EPGロード待機中として予約検証を行わない

- 起動直後は "EPGロード待機中" ログを出力

- 当日予約および開始30分以内の予約は EPG未一致でも解除しない

- 起動直後の誤解除を抑えた最終安定化版





v1.6 FINAL Silent 修正点:

- 通常時ログを最小化（通知・進捗ログを抑制）

- 切替成功 / BonDriver跨ぎ切替 / 重要な失敗のみログ出力

- 起動直後の EPG 待機を 180 秒に延長して堅牢性を向上

- 当日予約や近接予約は安易に削除せず保持優先

- UI・メニュー・通知なしの静音常駐版





v1.6.1 fixed 修正点:

- 同時有効予約の優先選択ロジックをビルド可能な形で修正

- 1周期1件だけ切替の動作を維持





v1.6.2 FINAL Silent 修正点:

- 一度切り替えた予約は、そのTVTest起動中は再切替しない

- 放送中に手動でチャンネルを変えても、同じ予約で戻され続ける問題を抑制

- 同一予約の判定は OriginalID と開始時刻ベースで実施





v1.7 LTS 追加調整:

- 同時有効予約の優先順位を安定化（同時刻は後から登録された予約を優先）

- BonDriver跨ぎ探索は原則として開始5分前以内または放送中のみ実施

- 既に目的サービスを視聴中なら再選択しない

- アイドル時の監視間隔を少し延ばして負荷を軽減

- UIなし・通知なし・静音動作の長期安定版



v1.7.1 UltraStable 追加整理:
- v1.7 LTS をベースにした最終配布版
- 視聴予約は1回のみ切替
- 地上波 / BS / CS の BonDriver 跨ぎ切替対応
- 起動直後の EPG 未準備時は待機して誤解除を抑制
- UIなし / 通知なし / 静音動作
- TvRock の録画用 TVTest とは分離運用を想定
- INI 形式は v1.7 LTS と互換
