# loop() 非依存化・ギャップレスログ 仕様書 (ドラフト)

Status: **仕様確定 (Q1〜Q13 全決定, 2026-10-06) — 実装 GO 待ち。** Phase 1 から順に PR を分けて進める。
作成: 2026-10-06。関連: [etc_experiment_mode_spec.md](etc_experiment_mode_spec.md) (記録の消費側)、
[etc_control_improvement_plan.md](etc_control_improvement_plan.md) §6 (同定計画。ギャップレス記録の動機)、
CLAUDE.md「ファームウェア並行性」節 (本書で更新対象)。

## 1. 目的とスコープ

実機の SD ログ解析 (2026-10-06, `dc_log_0002..0004.bin`) で、`loop()` が周期的に停止している
ことが分かった。本ファームは「制御は ISR、監視は loop」の分担なので、停止中もスロットル PID は
回り続けるが、**安全層 (アーミング / SHUTDOWN)、シフターのパルス出力、記録**が止まる。

本書は次の 4 本柱で「loop() を止めない / 止まっても困らない」構造にする:

| 柱 | 内容 | 効果 |
|---|---|---|
| **A. USB 送信ゲート** | ホストが読んでいない時は送信しない | 120 ms 停止の根絶 |
| **B. 計測** | loop / SD / 安全層 ISR の所要時間を毎秒集計して telemetry と SD ログに載せる | 停止の可視化。以後の判断材料 |
| **C. 安全層 ISR 化** | アーミング層・プラウシビリティ・スイッチ読み・シフターを 1 kHz タイマー ISR (`safetyISR`) へ | loop 停止が安全層とシフターに影響しなくなる |
| **D. ギャップレスログ** | 記録採取を ISR + リングバッファ化、SD ファイル事前確保、sync 間隔延長 | SD ストールで記録が途切れない |

スコープ外: SD 書き込みの完全非同期化 (Teensy の SdFat に非同期 API が無い)、CAN 制御入力 (凍結中)、
Launch Control (凍結中)、既存の double 書き換え競合 (§6.5 で指摘のみ、Q12)。

## 2. 根拠となる計測結果

| 事象 | 頻度 | 大きさ | 原因 (確定) |
|---|---|---|---|
| USB 送信タイムアウト | ホストがポートを閉じるたび 1 回 | **ちょうど 120 ms** | `usb_serial.c` の `TX_TIMEOUT_MSEC 120`。`availableForWrite()` は書き込み中の先頭バッファを除外して空きを返すため droppable 判定をすり抜け、先頭バッファ完了待ちで `usb_serial_write` がブロック。タイムアウト後は `transmit_previous_timeout` ラッチで以後は即 return |
| SD flush | 1.00 回/s (間隔中央値 1000.0 ms) | 中央値 4〜5 ms、p95 6 ms | `SYNC_INTERVAL_MS=1000` の `sdFile.flush()` = SdFat `sync()` (データブロック + ディレクトリエントリ)。ファイル肥大で 3.5→4.6 ms に増加 |
| セクタ書き込み遅延 | 約 0.25 回/s | 3〜9 ms | flush 周期と無関係な位相。カード内部処理 |
| 中間 | 2 時間で 17 件 | 21〜49 ms | 遅い flush とカードのブロック消去等 |

欠落合計 0.5〜0.7 %。USB の 120 ms は列挙されたホストが読み出しをやめた時だけ発生し、
車両単体 (ホスト無し) では `usb_configuration == 0` で即 return するため発生しない。

loop 停止の影響 (現状の配置):

| loop() の役割 | 5 ms 停止 | 120 ms 停止 |
|---|---|---|
| プラウシビリティ → `stopEtc()` / SHUTDOWN リレー | 反応 5 ms 遅れ | 反応 120 ms 遅れ (モーター ISR は直前の判断のまま) |
| SIG_IN (外部 AND) への反応 | 同上 | 同上 |
| シフターのパルス出力 (終端を `update()` の `millis()` 判定で切る) | 幅が最大 5 ms 伸びる | **25 ms パルスが最大 145 ms に** |
| CAN 送信 60 Hz | ジッタ | 7 フレーム欠落 |
| 同定実験 FSM | `millis()` 基準なので無影響 | 同上 (記録窓に穴) |
| SD ログ | 5 ms 欠落 | 120 ms 欠落 |

## 3. 全体像と段階

```
            ┌──────────── ISR (優先度) ────────────┐   ┌──────── loop() (非 ISR) ────────┐
  1 ms  prio 0    motor:      PID → PWM            │   │ IMU 読み / CAN poll+send / pulse 更新
  62.5µs prio 208 sampling:   ADC DMA → 移動平均    │   │ 同定実験 FSM tick / launch(凍結)
  1 ms  prio 64   safetyISR:  ①スイッチ読み        │   │ telemetry 送信 (DTR ゲート)
        (新設)                ②モード反映          │   │ SD: リング → 書き出し / 定期 sync
                              ③plausibility       │   │ コマンド受信・ディスパッチ
                              ④アーミング(①②③④)  │   │ シフター/監視のイベント → debug 送信
                              ⑤autoShifter.update │   │ 計測集計 (毎秒)
                              ⑥LogRecord 採取→リング│   └──────────────────────────────────┘
```

- **Phase 1 = A + B** (小さく即効。既存構造は不変)
- **Phase 2 = C** (構造変更。B の計測で ISR 所要時間を確認しながら)。名前は `safetyISR` (安全層 ISR): 既存の `motorControlISR` / `sensorSamplingISR` と同じ命名規則で、CLAUDE.md の「SHUTDOWN 安全層」を毎 ms 回す ISR であることを示す
- **Phase 3 = D** (C の安全層 ISR に採取を乗せる。事前確保・sync 延長・デコーダ)

各フェーズは独立にコミット・検証する。Phase 2 以降は CLAUDE.md の並行性節を更新する。

## 4. A: USB 送信ゲート

- `SerialProtocol::encodeAndWrite()` の冒頭で **`Serial.dtr()` が 0 なら全フレームを捨てる**
  (droppable / 非 droppable を問わず。読み手がいないので応答・debug も無意味)。
  既存の `availableForWrite()` 判定はそのまま残す。
- `Serial.dtr()` は `usb_cdc_line_rtsdtr` のフラグ読み (`operator bool` と違い `yield()` も 15 ms の
  整定待ちも無い)。
- console (`dc-console/src/serial.ts`): `port.open()` の直後に
  `port.setSignals({ dataTerminalReady: true, requestToSend: true })` を明示する
  (Web Serial の既定挙動に依存しない。Q1 決定)。pyserial は既定で DTR を立てる。
- DTR の扱い: ホストのアプリがポートを閉じると **OS のドライバが自動で DTR を下げる**。デバイスは
  それを `Serial.dtr()` で検知する。console 側に「読み出しをやめる時に DTR を落とす」処理は不要。
- 残る穴: ポートを開いたままアプリが固まって読まない場合は DTR が立ったままなので 1 回だけ 120 ms
  止まる (`transmit_previous_timeout` ラッチで 2 回目以降は即 return)。Phase 2/3 以降は loop 停止が
  安全層と記録に影響しないため許容する。
- 検証: ログ中にホストがポートを閉じても SD ログに 120 ms ギャップが出ないこと。
  ホストがポートを開いていない間はテレメトリが出ないこと (console 接続で再開)。

## 5. B: 計測 (loop / SD / ISR 所要時間)

### 5.1 計測点

| 名前 | 計測 | 集計 (1 s 窓) |
|---|---|---|
| `loop_us` | `loop()` 1 周の `micros()` 差 | max / mean |
| `sd_us` | `SdBinaryWriter::write()` と `flush()` の各呼び出し所要 | max |
| `safety_us` | 安全層 ISR 1 回の所要 (Phase 2 以降) | max |
| `log_drops` | リング溢れで捨てたレコード数 (Phase 3 以降) | 累積 (uint16 ラップ) |

`util/loop_stats.{hpp,cpp}` に `LoopStats` を新設し、`loop()` 末尾で `tick()`、毎秒 `rollover()`。
ISR 側は `volatile uint32_t` に max を書くだけ。

### 5.2 出力先

- **telemetry**: `State` に `SysStats sys = 4` を追加 (`loop_max_us / loop_mean_us / sd_max_us /
  safety_max_us / log_drops / loop_max_us_boot`、全て uint32)。直近に完了した 1 s 窓の値を 50 Hz の State に同梱 (Q2 決定)。
  `loop_max_us_boot` は起動以来の最大で、ポート開閉後に 120 ms 停止が無くなったことを SD を抜かずに確認するための値。
- **SD ログ**: LogRecord **v4** (136 → 148 B): `uint32 loop_max_us, sd_max_us` + `uint16 safety_max_us, log_drops`
  を末尾に追加 (4 バイト整列維持。µs を uint16 にすると 65 ms で飽和し、肝心の 120 ms 停止が記録できないため
  loop/SD は uint32)。値は直近 1 s 窓のもの (全レコード同値で構わない。
  レコード単位で持つのは「窓の切り出し」を楽にするため)。`SENSOR_LOG_VERSION` 4、
  `decode_log.py` 対応。
- **console**: DebugLog 見出し付近に `loop max / SD max / drops` の読み出し 1 行 (Q3 決定)。

## 6. C: 安全層 ISR (安全層の ISR 化)

### 6.1 構成

- 「安全層 ISR」(`safetyISR`): motor / sampling と同じ `IntervalTimer` コールバック (3 本目の PIT) で、
  `loop()` が担っていた監視と判断を毎 ms 確実に回す ISR。RTOS のタスクではない。
- `IntervalTimer safetyTimer`、周期 **1 ms**、NVIC 優先度 **64** (motor 0 より低く、
  USB 128 / sampling 208 より高い。USB に遅らされない。Q4 決定)。PIT は 4 本中 2 本使用 → 3 本目。
- 所要時間目標 **< 50 µs** (B の `safety_us` で実測・監視)。

### 6.2 loop から安全層 ISR へ移すもの (順序はこのまま)

1. スイッチ読み: `shutdownSig / modeSwitch / autoShiftSwitch` の `read()` (デバウンスは `millis()`
   基準で ISR 内でも成立)。`SensorHub::readImu()` からは外す。
2. モード反映: `selectToEtcMode()` → `EtcTarget::setMode*()`。
3. プラウシビリティ: `plausibilityValidator.isValid()` (内部で `isCurrentlyValid()`)。
   **loop 側は `isCurrentlyValid()` を呼ばない** (副作用あり)。telemetry / launch 用に
   副作用なしの `validLatched() const` を追加。
4. アーミング ①②③④ とリレー。`startEtc()` / `stopEtc()` は安全層 ISR からのみ呼ぶ
   (呼び出しコンテキストを 1 つにして競合を消す)。
5. `autoShifter.update(autoShiftOn)` 全体 (入力エッジ検出・判断・出力整形)。(Q11 決定: 丸ごと移す。
   ただしオートシフターは将来 CAN 側 (IST) へ移す予定なので、作り込みは最小限に留める)

loop に残るもの: IMU 読み、CAN poll/send、`updatePulse()`、`experimentRunner.tick()`、
launch (凍結)、telemetry、SD 書き出し + sync、コマンド処理、イベントの debug 送信、計測集計。

### 6.3 ISR 内の禁止事項

`Serial` / `sendDebugf` / SD / flash / `malloc` / `delay` を呼ばない。`AutoShifter` の
`sendDebugf` 4 箇所は **イベントキュー** (`ShiftEvent { kind, gear, widthMs }`、8 要素リング) に
置き換え、loop が `autoShifter.pollEvent()` で取り出して `sendDebugf` する。

### 6.4 startEtc / stopEtc を ISR から呼ぶ際の注意

- `stopEtc()`: `setMotorOff()` (PWM 0・SLP/リレー LOW) → `motorControlTimer.end()`。
  motor ISR (優先度 0) が間に割り込んでも `cycle()` の `write` は `_isOn=false` で no-op。
- `startEtc()`: `setMotorOn()` (`pid.reset()`) → `begin()`。`IntervalTimer::begin/end` は PIT
  レジスタと NVIC の操作のみで ISR から呼べる。全呼び出しが安全層 ISR に集約されるため
  loop との競合は無い。

### 6.5 共有状態 (書き手 → 読み手)

| 状態 | 書き手 | 読み手 | 幅 | 扱い |
|---|---|---|---|---|
| `EtcTarget::mode` | 安全層 ISR | motor ISR, loop | enum (1 B) | アトミック |
| `EtcTarget::manualTarget` | loop (コマンド / 実験) | motor ISR, 安全層 ISR | **double** | 既存の競合 (Q12 決定: 別課題、競合は許容) |
| plausibility check flags | loop (configurator / 実験) | 安全層 ISR | bool | アトミック |
| `experimentRunner.wantsCoast()` | loop | 安全層 ISR | bool | アトミック |
| validator 内部 (時刻・ラッチ・errorBits) | 安全層 ISR | loop (telemetry / log) | u16 / bool | アトミック (const getter 経由) |
| `autoShifter.state()` | 安全層 ISR | loop (log) | enum | アトミック |
| スイッチ `isOn()/getStatus()` | 安全層 ISR | loop (CAN TX / log) | bool / enum | アトミック |
| `motorController.isOn()` | 安全層 ISR (start/stop) | loop (無し) | bool | — |
| センサー校正 (slope/intercept) | loop (`calibrate()`) | 全 ISR | **double** | 既存の競合 (Q12 決定: 別課題、競合は許容) |

## 7. D: ギャップレスログ

### 7.1 採取 (ISR)

- 安全層 ISR の最後 (⑥) で `buildLogRecord(...)` を呼び、リングに書く。同じ tick の判断結果が
  レコードに反映される。`buildLogRecord` は const getter 読みの純粋関数で、読む値はほぼ 32 bit
  単位 (MovingAverage は `noInterrupts()` で保護済み)。
- `millis()` ベースの 1 kHz ではなくタイマー周期で採取するので、`t_ms` は `millis()` を記録しつつ
  サンプル間隔はタイマー精度になる。

### 7.2 リングバッファ

- `DMAMEM static LogRecord ring[LOG_RING_RECORDS]`、**2048 レコード (≈2 s, 295 KB @144 B)**。
  RAM2 空き約 500 KB (現ビルド)。(Q5 決定)
- head (ISR が書く) / tail (loop が読む) は `volatile uint32_t`。満杯時は**新しいレコードを捨てて**
  `log_drops++` (順序を保つ。古い方を捨てる方式は tail の競合管理が要る。Q6 決定)。
- 記録窓 (実験) との関係: 2 s のリングは観測済み最大ストール (121 ms) と SD の多ブロック書き込みを
  十分吸収する。

### 7.3 書き出し (loop)

- `SensorLogger::service()` が tail から **4 KB 以上たまったら最大 8 KB** をまとめて
  `sdFile.write()` (多ブロック書き込みで効率化。1 回の所要を B で計測し上限を調整。Q7 決定)。
- `sync()` 間隔 `SYNC_INTERVAL_MS` を **1000 → 2000** (電源断時の損失上限 2 s ≈ 290 KB。Q8 決定)。
  事前確保 (7.4) と合わせて flush 自体も軽くなる見込み。

### 7.4 ファイル事前確保

- `SD.open()` の代わりに `SDClass::sdfs` (SdFs) 経由で `FsFile` を開き (`O_RDWR|O_CREAT|O_TRUNC`)、
  `preAllocate(LOG_PREALLOC_BYTES)` で連続クラスタを確保する (FAT 更新が書き込み経路から消える)。
  サイズ **512 MB ≈ 1 時間** (144 B × 1 kHz)。上限到達時は新ファイルを開く。(Q9 決定)
  (`SDClass::sdfs` が public か要確認。非公開なら `SdFs` を自前で `begin(BUILTIN_SDCARD)`)。
- **電源断対策**: 確保領域の末尾は未初期化データなので、デコーダが終端を知る手段が要る。
  - `LogHeader` v4 に `uint32 record_count` を追加 (32 B)。`sync()` の直前にヘッダへ seek して
    更新 (+1 ブロック書き込み/sync)。
  - デコーダは `record_count > 0` ならそれを使い、0 (初回 sync 前の電源断) なら
    `t_ms` の単調性 (前レコード以上かつ +10 s 未満) が崩れた所で止める。
  - 正常終了 (reboot コマンド) 時は `truncate()` + `close()`。
- 起動時の既存ファイル検出 (`dc_log_NNNN` 採番) は不変。

### 7.5 検証

- 10 分のログ中にポート開閉・コマンド送信・`set_config` (flash 保存) を混ぜても
  `t_ms` 差分が全て 1 ms (ギャップ 0)、`log_drops == 0`。
- ストール注入 (§8) 500 ms → ギャップ 0 / drops 0。3 s → drops が計上され、その後記録再開。

## 8. 検証用デバッグコマンド (ベンチ専用)

「ストール注入」= SD や USB の停止を人工的に再現する**ベンチ専用コマンド**。
`DebugStallCmd { uint32 ms }` を追加し、loop 内で `delay(ms)` する (**`-DBENCH_DEBUG` ビルドのみ
登録**、本番は ok=false。Q10 決定: 含める)。これで次を実機で確認できる:

- Phase 2: 200 ms ストール中に SIG_IN を落とすと ETC が 1 ms 以内に止まる (SD ログの flags/duty で確認)。
  シフターのパルス幅がストールの影響を受けない (ログの `autoshift_state` 遷移時刻で確認)。
- Phase 3: ストール中も記録が途切れない。

## 9. 実装タッチポイント

| ファイル | Phase | 変更 |
|---|---|---|
| `src/serial/serial_protocol.cpp` | 1 | `encodeAndWrite` に `Serial.dtr()` ゲート |
| `dc-console/src/serial.ts` | 1 | `setSignals({dataTerminalReady:true, requestToSend:true})` |
| `src/util/loop_stats.{hpp,cpp}` | 1 | 新規: 所要時間集計 |
| `src/util/log/sd_binary_writer.cpp` | 1 / 3 | write/flush 所要計測 / `FsFile` + `preAllocate` + ヘッダ `record_count` |
| `spec/proto/drive_controller.proto` | 1 / 2 | `SysStats`、`DebugStallCmd` → 両側再生成 |
| `src/util/log/log_record.hpp` + `tools/decode_log.py` | 1 / 3 | v4 (stats フィールド / ヘッダ `record_count`、終端判定) |
| `src/main.cpp` | 2 / 3 | 安全層 ISR タイマー、loop の再編、イベント送信 |
| `src/sensor/sensor_hub.{hpp,cpp}` | 2 | `readImu()` からスイッチ読みを分離 (`readSwitches()`) |
| `src/etc/plausibility_validator.{hpp,cpp}` | 2 | `validLatched() const` |
| `src/shift/auto_shifter.{hpp,cpp}` | 2 | `sendDebugf` → イベントキュー + `pollEvent()` |
| `src/util/log/sensor_logger.{hpp,cpp}` | 3 | リング採取 API (`captureFromIsr`) + まとめ書き出し + drops |
| `src/commands/command_controller.cpp` | 2 | `DebugStallCmd` (BENCH_DEBUG) |
| `dc-console/` | 1 | SysStats 表示 |
| `CLAUDE.md` | 2 / 3 | 並行性節 (安全層 ISR 追加、SD ログ節) |

## 10. 決定事項 (2026-10-06 レビュー)

| Q | 決定 |
|---|---|
| Q1 | console は open 直後に `setSignals` で DTR/RTS を明示。閉じる側は OS が DTR を落とすので処理不要 |
| Q2 | `SysStats` は 50 Hz の State に同梱 |
| Q3 | console 表示は読み出し 1 行 |
| Q4 | `safetyISR` (安全層 ISR) = 3 本目の IntervalTimer ISR、1 ms、優先度 64。名前は「安全のための ISR」と分かるものにする (supervisor は別概念と衝突、arming は一工程に過ぎないため不採用) |
| Q5 | リング 2048 レコード (≈2 s) を RAM2 に |
| Q6 | 溢れ時は新規を捨てて `log_drops` を数える |
| Q7 | 4 KB たまったら最大 8 KB をまとめ書き (実測で調整) |
| Q8 | `SYNC_INTERVAL_MS` = 2000 |
| Q9 | 事前確保 512 MB、上限到達で新ファイル |
| Q10 | ストール注入コマンドを `-DBENCH_DEBUG` ビルドにのみ登録 |
| Q11 | `autoShifter.update()` を丸ごと安全層 ISR へ。将来 CAN 側へ移す予定のため最小限の作り込み |
| Q12 | 既存の double 競合 (`manualTarget`、校正 slope/intercept) は**別課題**。競合は許容 |
| Q13 | PR #5 のブランチから派生した stacked ブランチで Phase 1 → 2 → 3 を PR 単位で進め、#5 マージ後にリベース |
