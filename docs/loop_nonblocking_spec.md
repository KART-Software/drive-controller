# loop() 非依存化・ギャップレスログ 仕様書

Status: **Phase 1 実装済み (PR #9, 2026-10-09 マージ)。Phase 2 / 3 は 2026-10-11 に全面改訂。Phase 2a 実装済み (2026-10-17 マージ)、2c 実装済み (2026-10-19)、2b / 3 は未着手。**
作成: 2026-10-06、改訂: 2026-10-11。関連: [control_input_spec.md](control_input_spec.md) (制御入力の CAN 化。Phase 2 の前提)、
[etc_experiment_mode_spec.md](etc_experiment_mode_spec.md) (記録の消費側)、
[etc_control_improvement_plan.md](etc_control_improvement_plan.md) §6 (同定計画。ギャップレス記録の動機)、
CLAUDE.md「ファームウェア並行性」「SD ロギング」節 (本書で更新対象)。

## 1. 目的とスコープ

実機の SD ログ解析 (2026-10-06, `dc_log_0002..0004.bin`) で、`loop()` が周期的に停止している
ことが分かった。さらに 2026-10-09 には、SD カード満杯で **loop が 1 周ごとに 504 ms 止まる** (約 2 Hz でしか回らない)
状態を実機で確認した (§2.2)。本ファームは「制御は ISR、監視は loop」の分担なので、停止中もスロットル PID は
回り続けるが、**安全層 (アーミング / SHUTDOWN)、シフター、CAN、記録**が止まる。

改訂後の方針は「**時間の決まった処理はすべて割り込みで回し、loop には遅れてよい処理だけを残す**」。
loop が SD で何百 ms 止まっても、遅れるのは console 送信とコマンド応答だけになる。

| 柱 | 内容 | Phase |
|---|---|---|
| **A. USB 送信ガード** | TX リングに空きが無い時は送らない (全フレーム) | 1 (済) |
| **B. 計測** | loop / SD / ISR の所要時間を毎秒集計して telemetry と SD ログに載せる | 1 (済)、2 で ISR 分を追加 |
| **C. 割り込みの階層化** | タイマーを分けて優先度を実際に効かせる。安全層・シフター・CAN・パルスカウンタ・IMU を割り込みへ | 2 |
| **D. ギャップレスログ** | 記録を 1 ms 割り込みで採取してリングバッファへ。SD は exFAT・起動ごとのディレクトリ・100 MB 事前確保ファイル | 3 |

ベアメタル (Arduino / Teensy コア) のまま進める (2026-10-11 決定。RTOS 移行は見送り)。

スコープ外: SD 書き込みの非同期化 (SdFat に非同期 API が無い。loop が止まってよい構成にして不要にする)、
Launch Control (凍結中)、
既存の double 書き換え競合 (§6.6、Q12)。

## 2. 根拠となる計測結果

### 2.1 loop 停止 (2026-10-06, Phase 1 の根拠)

| 事象 | 頻度 | 大きさ | 原因 (確定) |
|---|---|---|---|
| USB 送信タイムアウト | ホストがポートを閉じるたび 1 回 | **ちょうど 120 ms** | `usb_serial.c` の `TX_TIMEOUT_MSEC 120`。telemetry が空き判定の対象になっておらず (呼び出し側が一度も `droppable=true` を渡していない) 常にブロッキング送信だったため、読み手が消えると先頭バッファの完了待ちで `usb_serial_write` がブロック。タイムアウト後は `transmit_previous_timeout` ラッチで以後は即 return |
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

### 2.2 SD 満杯時の停止 (2026-10-09)

- 症状: telemetry の `loop_max_us` ≈ `sd_max_us` ≈ 504 ms、telemetry の到着間隔が 504 ms の倍数。
  loop が 1 周ごとに 504 ms 止まっていた。
- 原因 (MTP でカードを確認して確定): 16 GB (FAT32) が空き 0 B。ファイルがクラスタ境界に来るたびに SdFat の
  `allocateCluster()` が FAT 全体 (約 3800 セクタ) を読んで空きを探し、見つからず失敗する。`write()` は失敗を覚えず、
  ファームも戻り値を見ていないので、次の周でまた全探索する。
- 副次的に、1 ファイルが FAT32 の上限 4 GiB に達していたもの (`dc_log_0018.bin` = 4,294,967,276 B) も見つかった。
  こちらは即座に失敗するので loop は止まらないが、ログは黙って途切れる。

### 2.3 CPU 使用率 (2026-10-11, ベンチ・ETC 動作中・CAN 無通信)

DWT サイクルカウンタで 1 s 窓ごとに集計 (一時計測、コミットなし)。

| 処理 | 割合 | 1 回の最大 |
|---|---|---|
| モーター ISR (1 kHz) | 0.13 % | 1.3 µs |
| サンプリング ISR (16 kHz) | 1.5 % | 0.9 µs |
| loop: IMU 読み (SPI ブロッキング、毎周 = 約 35,000 回/s) | 61.5 % | 20 µs |
| loop: CAN 受信ポーリング (毎周) | 25.7 % | 11 µs |
| loop: 安全層 | 5.9 % | 5 µs |
| loop: オートシフター | 1.5 % | 3.5 µs |
| loop: SD | 1.2〜2.1 % | **7.2 ms** |
| loop: telemetry | 0.4 % | 69 µs |

必要な処理だけを数えると 5 % 前後 (IMU を 8 kHz DMA、CAN を受信割り込みにした場合)。割り込みへ移しても CPU は足りる。

### 2.4 IntervalTimer の共有 IRQ

PIT の 4 チャネルは割り込み番号 `IRQ_PIT` を 1 つしか持たない。`IntervalTimer::priority()` は IRQ 全体の優先度を
「全チャネル中で最も高いもの」にし、コールバックは 1 つの関数から順に呼ばれる。現状の
「motor = 0、sampling = 208」は実際には **両方とも優先度 0** で動き、互いに割り込めない。
旧版 §6 の「3 本目の IntervalTimer で安全層 ISR (優先度 64)」もこの理由で成立しない。

## 3. 全体像と段階

### 3.1 実行コンテキスト (Phase 2 / 3 完了後)

NVIC の優先度は上位 4 bit のみ有効 (0, 16, … 240 の 16 段階)。数字が小さいほど優先。

| 優先度 | コンテキスト | タイマー / 起点 | 内容 |
|---|---|---|---|
| 48 | FlexPWM 入力キャプチャ | 車輪速のエッジ | FreqMeasureMulti 内部 (ライブラリ既定のまま) |
| 64 | `tickISR` | **GPT2**、1 ms | 安全層、**モーター制御 (PID → PWM)**、モード反映、シフターの時間管理と自動判断、CAN 送信 (kart-can の周期)、CAN 途絶判定、回転計測の更新 (毎 ms)、同定実験の進行、ログ 1 レコード採取 |
| 64 | `canRxISR` | FlexCAN3 の受信割り込み | 0x740 / 0x741 の取り込み。シフト入力のエッジは**その場で**シフターへ渡す |
| 128 | USB | (Teensy 既定) | — |
| 208 | `sensorSamplingISR` | **PIT** のチャネル 2 (PIT の割り込みはこれだけ)、16 kHz | ADC の DMA 取り込みと次の転送開始、**IMU の DMA (2 回に 1 回 = 8 kHz)** |
| — | `loop()` | — | SD 書き出し、telemetry 送信、コマンド処理、ISR からのイベントの debug 送信、計測窓の集計 |

- **`tickISR` と `canRxISR` は同じ優先度**にする。同優先度は互いに割り込めないので、シフター・`CanRxData`・
  `ControlInput` をこの 2 つの間で排他しなくてよい (保留側は相手の終了直後に走る。遅れは最大で相手の 1 回分 = 数十 µs)。
- **モーター制御は `tickISR` に統合する** (旧 `motorControlISR` は廃止)。安全層の判断の直後に回すので、tick の残りの処理を待たない。
  モーターだけ別の割り込みにすると、ETC の停止をタイマーの停止で行う必要があり、出力とタイマー停止の順序を気にすることになる。
- GPT1 / GPT2 は Teensy コアも使用中のライブラリも使っていない (確認済み)。GPT2 を tick に使い、GPT1 は空きのまま残す。PIT の割り込みはサンプリングだけになり、優先度 208 が実際に効く (チャネル 0 / 1 は時計の Lifetime Timer が割り込みなしで使う、§7.2)。
  その結果サンプリングは USB と制御 tick に割り込まれるようになる。62.5 µs を超えて待たされた回は 1 サンプル飛ぶが、
  DMA 転送自体は止まらず、移動平均のサンプル数が 1 つ減るだけ。

### 3.2 段階

- **Phase 1 = A + B** (済、PR #9)
- **Phase 2 = C**。前提: 制御入力の CAN 化 (`feature/can-control-input`, [control_input_spec.md](control_input_spec.md)) が先に入っていること。
  - 2a: タイマーの付け替え (`tickISR` → GPT2、PIT の割り込みはサンプリングだけ。チャネル 0 / 1 は時計) と、モーター制御・安全層・シフター・CAN・回転計測・同定実験の tick への移動、flash 保存の手順 (§6.8)
  - 2b: IMU の DMA 化 (8 kHz)
  - 2c: 回転センサー 6 つを周期計測へ (エンジン・クラッチ後は QuadTimer の入力キャプチャ、車輪は FreqMeasureMulti の周期列。§6.7)。ベンチで成立確認済み
- **Phase 3 = D** (SD: exFAT・ディレクトリ・100 MB 事前確保・リングバッファ・`t_us`)

各フェーズは独立にコミット・検証する。Phase 2 以降は CLAUDE.md の並行性節を更新する。

## 4. A: USB 送信ガード

- `SerialProtocol::encodeAndWrite()` で **全フレーム (telemetry / 応答 / debug)** に
  `Serial.availableForWrite() >= len` を要求し、満たさなければ捨てる。旧実装はこの判定を
  `droppable=true` のフレームにだけ掛ける設計だったが、telemetry を含め誰も true を渡しておらず
  判定は死んでいた (= 50 Hz telemetry が常にブロッキング送信)。これが 120 ms 停止の真因。`droppable` 引数は削除。
- 待たない根拠: `usb_serial_write` が待つのは「先頭バッファが満杯で次のバッファがまだ転送中」の時だけ。
  `availableForWrite()` は先頭以外の空きバッファ (2048 B 単位) の合計を返し、バッファはリング順に空く。
  フレーム最大 `DC_MAX_FRAME` < `TX_SIZE` (2048 B、`static_assert` で固定) なので、空きがあれば 1 フレームは必ず収まる。
- **優先度**: 応答と debug (Important) は `>= len`、telemetry (Bulk) は `>= len + 2048` を要求し、TX バッファ
  1 本分を応答用に残す。全フレームを同列に扱うと 50 Hz telemetry がリングを埋めて応答が捨てられ、
  コマンドは実行済みなのに console が 3 s タイムアウトする (2 回目の自己レビューで発覚)。`TxClass` は
  既定値を持たせず、旧 `droppable` のような付け忘れを防ぐ。
- 当初案の **DTR ゲート (`Serial.dtr()==0` で破棄) は撤回** (自己レビュー 2026-10-06):
  - DTR フラグは SET_CONTROL_LINE_STATE でしか更新されず、ケーブル抜去・バスリセット・サスペンドで
    クリアされない。「console を閉じずにケーブルを抜く」でストールが残る。
  - DTR を立てないホスト (pyserial の `dtr=False`、`pio device monitor` の `monitor_dtr=0`、.NET 既定) で
    コマンドは実行されるのに応答が返らなくなる。
  - 起動時の診断 (`SD log -> ...`、IMU WHO_AM_I、config 読込) がポート未オープン時に捨てられる。
    送信ガード方式なら TX リングに溜まり、後から開いたホストに届く (debug は Important なので先頭以外の
    3 本分まで。1 フレームが 1 本を占有するので約 3 件)。
- console は open 直後に `port.setSignals({ dataTerminalReady: true })` を明示する (Q1)。ファームは起動時に
  DTR を最大 3 s 待つので意味がある。RTS は使わないので触らない (RTS でリセットがかかる機器があるため)。
- 検証: ホストがポートを閉じる / DTR=0 で開く、のいずれでも `loop_max_us_boot` に 120 ms が出ないこと。
  DTR=0 のホストでもコマンド応答が返ること。

## 5. B: 計測 (loop / SD / ISR 所要時間)

### 5.1 計測点

| 名前 | 計測 | 集計 (1 s 窓) |
|---|---|---|
| `loop_us` | `loop()` 1 周の `micros()` 差 | max / mean |
| `sd_us` | `SensorLogger` の write / flush 1 回の所要 (呼び出しごとに `LoopStats` へ直接報告) | max |
| `safety_us` | tickISR 1 回の所要 (Phase 2 以降) | max |
| `log_drops` | リング溢れで捨てたレコード数 (Phase 3 以降) | 累積 (uint16 ラップ) |

`util/loop_stats.hpp` に `LoopStats` を新設し、`loop()` 先頭で `tick()`、その直後に毎秒 `rollover()`
(窓境界を loop 先頭に置き、同じ周の loop 所要と SD 所要を同じ窓に入れる)。
ISR 側は `volatile uint32_t` に max を書くだけ。

### 5.2 出力先

- **telemetry**: `State` に `SysStats sys = 4` を追加 (`loop_max_us / loop_mean_us / sd_max_us /
  safety_max_us / log_drops / loop_max_us_boot`、全て uint32)。直近に完了した 1 s 窓の値を 50 Hz の State に同梱 (Q2 決定)。
  `loop_max_us_boot` は起動以来の最大で、ポート開閉後に 120 ms 停止が無くなったことを SD を抜かずに確認するための値。
  注意: `save` / `set_config` の flash 消去 (割り込み禁止で約 150 ms、モーター ISR も止まる) もここに記録される。
- **SD ログ**: LogRecord **v4** (136 → 148 B): `uint32 loop_max_us, sd_max_us` + `uint16 safety_max_us, log_drops`
  を末尾に追加 (値は `LoopStats::live()` = 直前の窓と現在の窓のここまでの大きい方。telemetry も同じ値。停止直後のレコードから
  値が上がるので、CSV で `t_ms` の穴と突き合わせられる。4 バイト整列維持。µs を uint16 にすると 65 ms で飽和し、肝心の 120 ms 停止が記録できないため
  loop/SD は uint32)。停止の値は停止直後から次の窓の終わりまで (1〜2 s) のレコードに残る。`SENSOR_LOG_VERSION` 4、
  `decode_log.py` 対応。
- **console**: DebugLog 見出し付近に `loop max / SD max / drops` の読み出し 1 行 (Q3 決定)。


## 6. C: 割り込みの階層化 (Phase 2)

### 6.1 タイマー

- `util/gpt_timer.{hpp,cpp}` (新規): GPT を 1 本扱う小さなクラス (GPT2 を使う)。`begin(periodUs, isr, priority)` / `end()`。
  クロックは 24 MHz の固定源を使い、周期は整数カウントで誤差なし。`IntervalTimer` と同じく `begin` / `end` は
  レジスタと NVIC の操作だけなので ISR から呼べる。
- `motorControlISR` と `IntervalTimer motorControlTimer` は廃止し、モーター制御は `tickISR` の中で回す (§6.2 の 6)。
  `startEtc()` / `stopEtc()` はタイマーを触らず、`MotorController` の ON / OFF (`setMotorOn()` は内部で `pid.reset()`、
  `setMotorOff()` は PWM 0) だけを行う。tick は「ON なら `cycle()` を呼ぶ」。
- `tickISR`: GPT2 (優先度 64)、`setup()` の最後で開始し、以後止めない。
- `sensorSamplingISR`: PIT のまま (チャネル 2。PIT の割り込みはこれだけなので優先度 208 が効く。チャネル 0 / 1 は時計の Lifetime Timer が割り込みなしで使う)。

### 6.2 `tickISR` (1 ms) — 実行順

| 順 | 処理 | 周期 | 旧所在 |
|---|---|---|---|
| 1 | SIG_IN 読み (`shutdownSig().read()`) | 毎回 | loop (`readImu` 内) |
| 2 | 制御入力: `ControlInput::update()` (新しいフレームの取り込み・途絶時のフォールバック、PR #12) | 毎回 | loop |
| 3 | モード反映 (`ControlInput::etcMode()` → `EtcTarget`) | 毎回 | loop |
| 4 | プラウシビリティ (`plausibilityValidator.isValid()`) | 毎回 | loop |
| 5 | アーミング ①〜⑤ と SHUTDOWN リレー。`startEtc()` / `stopEtc()` は**ここからのみ**呼ぶ (⑤ = flash 保存の待機、§6.8) | 毎回 | loop |
| 6 | **モーター制御** (`motorController.cycle()`。ON のときだけ。PID → PWM) | 毎回 | motor ISR (PIT、優先度 0) |
| 7 | 同定実験の進行 (`experimentRunner.tick()`) | 毎回 | loop |
| 8 | シフターの時間管理 (パルス終端・最短間隔・自動判断) (`autoShifter.tick()`) | 毎回 | loop (`update`) |
| 9 | 回転計測の更新 (`sensorHub.updatePulse()`。車輪速・エンジン・クラッチ後、§6.7) | **毎回** | loop (100 ms) |
| 10 | CAN 送信 0x600〜0x603 / 0x60A (DC_Status)。周期は kart-can の `KART_CAN_*_CYCLE_TIME_MS` をそのまま使う | 16 ms / 33 ms | loop |
| 11 | ログ 1 レコード採取 → リング (Phase 3) | 毎回 | loop |
| 12 | 自身の所要時間を計測 (`LoopStats` の ISR 枠) | 毎回 | — |

- 時刻: ISR 内の周期判定は tick 回数 (1 ms 単位の `uint32_t` カウンタ) で行い、`millis()` の読み直しはしない。
  ログの時刻は PIT の Lifetime Timer から作る (§7.2)。
- 1〜5 は数 µs なので、モーター制御の開始時刻のずれも数 µs (1 ms 周期の PID には影響しない)。
- 所要時間の目標 **< 50 µs** (安全層 + モーター + 送信 + 採取。2.3 の実測から 10〜20 µs 程度の見込み)。B の計測で監視する。
- 同定実験の開始 / 停止は loop (コマンド) からの**要求フラグ**にし、tick が取り込んで状態を変える
  (実験の状態遷移の書き手を tick だけにする)。

### 6.3 `canRxISR` — CAN 受信

- FlexCAN_T4 を割り込み受信にする (`enableMBInterrupts()` + `onReceive()`)。**`events()` は呼ばない**:
  呼ばない場合、ライブラリは受信割り込みの中でそのままコールバックを呼ぶ (`FlexCAN_T4.tpp` の `struct2queueRx` で確認済み)。
  `begin()` の後で NVIC 優先度を 64 に設定する (ライブラリは設定せず、Teensy 既定の 128 のまま)。
- 0x740 (Control): `CanRxData` に取り込むだけ。モードの反映は次の tick (1 ms 以内)。
- 0x741 (Shift): 取り込み、UP / DOWN の押下状態を**その場で** `autoShifter.onPaddles()` に渡し、立ち上がりならパルスを開始する。
  パルスの開始時刻は受信した瞬間の時刻 (`sysClock.nowUs()`) にする。直前の tick の時刻を使うと、終端の判定が
  最大 1 ms 早まり、パルスが短くなる (IST の 15 ms が 14 ms に)。終端は tick で判定するので、幅は指定値〜指定値 + 1 ms。
  チャタリング除去は送信側 (data-logger) の責務なので、受信側ではしない。
  送信側は 10 ms 周期で押下状態を送り、状態が変わった瞬間にも即時に 1 フレーム追加で送る (kart-can can-spec.md §4.1)。
  受信側は周期フレームか追加フレームかを区別せず、0→1 の立ち上がりだけを要求とみなす (直後の周期フレームで二重にシフトしない)。
- **パルス出力の最短間隔 0.5 s**: 前のパルスの開始から 0.5 s 経つまで次のパルスを出さない。間隔内に来た入力は捨てる
  (ためて後で出すと、ドライバーの意図と違うタイミングでシフトするため)。既存の `cooldown_ms` (パルス終了後の待ち、
  既定 200 ms) はこの最短間隔に置き換え、config の既定値を 500 にする。
- 送信 (§6.2 の 10) は tick から行う。受信と送信が同じ優先度なので、FlexCAN のメールボックス操作は競合しない。
  loop は CAN に一切触らない。

### 6.4 IMU の DMA 化 (Phase 2b)

- 現状: loop が毎周 SPI1 でブロッキング読み (1 回 20 µs、約 35,000 回/s、CPU の 61 %)。
- 変更: ADC と同じ方式にする。`sensorSamplingISR` (16 kHz) の 2 回に 1 回 (= **8 kHz**、ジャイロの内部更新レートの上限) で
  SPI1 の DMA 転送 (加速度 + 温度 + ジャイロの 14 B + アドレス 1 B、8 MHz で約 15 µs) を開始し、次の回に前回の結果を
  移動平均へ反映する。加速度の更新は 4 kHz なので同じ値を 2 回取り込むことがあるが、平均に影響はない。
- 移動平均の窓 **8 サンプル (= 1 ms 分)** 経由で、telemetry・CAN・ログは平均値を読む (`MovingAverage<N>` の `noInterrupts()` 保護を踏襲)。
- 初期化 (`WHO_AM_I` 確認とレジスタ設定) は今どおり `setup()` でブロッキング。
- `SensorHub::readImu()` は削除。

### 6.5 ISR 内の禁止事項と、loop への伝達

- ISR (`tickISR` / `canRxISR` / サンプリング) から `Serial` / SD / flash / `malloc` / `delay` を呼ばない。
  いずれも中で割り込みや完了を待つので、上の優先度から呼ぶと固まる。
- debug は `DebugLogger::log()` で出す (どの文脈からでもよい)。`DebugLogger` は時刻を付けて出力先 (`LogWriter`) に配るだけで、
  その場で出せるかは出力先が判断する。USB へ送る `SerialDebugWriter` は、ISR の中や割り込み禁止中に呼ばれたら
  (IPSR / PRIMASK で判定)、または USB がつながっていない間 (`!Serial`) は固定長のキューに積み、loop の `DebugLogger::service()` (→ 各 writer の `service()`) が送る。
  それ以外はその場で送る。ISR から呼ぶときの書式は整数と文字列だけ。
  各メッセージには `log()` が呼ばれた時刻 (µs、`DebugMessage.timestamp_us`) を付ける。キュー経由のものは後から出るので、
  届く順は呼ばれた順と前後しうる。並べ替えは読む側 (console) が時刻で行う。対象: シフター、同定実験 (開始 / 中断理由 / 終了)、`ControlInput` (上書きの自動解除)。

### 6.6 共有状態 (書き手 → 読み手)

| 状態 | 書き手 | 読み手 | 扱い |
|---|---|---|---|
| `EtcTarget::mode` | tick | tick (モーター制御), loop | 1 B、アトミック |
| `EtcTarget::manualTarget` | loop (コマンド)、tick (実験) | tick (モーター制御) | **double**。既存の競合 (Q12: 別課題) |
| `CanRxData` | canRxISR | tick (同優先度で排他不要)、loop (telemetry) | loop 側は多少の不整合を許容 (表示のみ) |
| `ControlInput` の override | loop (コマンド) | tick | 構造体の書き込みを `noInterrupts()` で囲む (数 µs) |
| プラウシビリティのチェックフラグ | loop (`setCheckFlags`) | tick | bool、アトミック |
| validator 内部 (ラッチ・errorBits) | tick | loop (telemetry) | const getter 経由、アトミック |
| シフターの状態 | tick, canRxISR | loop (telemetry) | enum、アトミック |
| 回転計測の結果 | tick | loop (telemetry)、tick (ログ) | float、アトミック |
| IMU / ADC の移動平均 | サンプリング ISR | tick, loop | 既存の `noInterrupts()` 保護 |
| ログのリング | tick (書き) | loop (読み) | 書き手 1・読み手 1。head / tail を `volatile uint32_t` で |
| 実験の開始 / 停止要求 | loop | tick | 要求フラグ (§6.2) |
| センサー校正 (slope / intercept) | loop (`calibrate()`) | 全 ISR | **double**。既存の競合 (Q12: 別課題) |

| flash 書き込みの要求 / 準備完了 | loop / tick | tick / loop | bool 2 つ (§6.8) |

flash への書き込みは割り込み禁止で約 150 ms かかり、その間は tick・CAN 受信を含むすべての割り込みが止まる (§6.8)。

### 6.7 回転計測 (Phase 2a / 2c)

動機: 将来の Launch Control はエンゲージ率 (車輪側回転 ÷ エンジン回転) をフィードバックするので、回転の遅れと分解能が効く。
現状のエンジン回転・クラッチ後回転は「100 ms 窓のパルス数 ÷ 100 ms」で、遅れ約 50 ms・分解能 50 rpm (エンジン 12 歯)。
窓を縮めると分解能が落ちる (20 ms で 250 rpm)。

| センサー | 機構 | 歯数 | 最大パルス |
|---|---|---|---|
| 車輪速 FL/FR/RL/RR | FreqMeasureMulti (FlexPWM 入力キャプチャ、周期計測) | config | — |
| エンジン (pin 14) | QuadTimer3 ch2 | 12 (等間隔) | 2.4 kHz (12,000 rpm) |
| クラッチ後 (pin 15) | QuadTimer3 ch3 | 100 (等間隔) | 20 kHz (エンジンと同回転と仮定) |

- **2a**: 更新を tick の毎回にする。車輪速は周期計測なので精度は変わらない。エンジン・クラッチ後は当面、毎 ms のカウント値を
  リングに残し「今 − 100 ms 前」で求める (分解能は現状と同じ、100 ms ごとの段差が消える)。
- **2c**: 6 つの回転センサー (エンジン・クラッチ後・車輪 4 輪) を「何番目のエッジがいつ来たか」で揃え、同じ計算で回転を出す。
  将来の Launch Control (エンゲージ率 = クラッチ後 ÷ エンジン) で使えるよう、低回転の立ち上がり・止まりかけ・時刻の揃いを重視する。
  **ピンは変えない。**
  - 構成 (3 層):
    - `RotationSensor` (`interfaces/sensor/rotation_sensor.hpp`): 使う側に見せる口。`sample()` で
      `RotationSample { hz, rpm, timeUs (窓の中心、Clock の時間軸), windowUs, edges }` を返す。`hz()` / `rpm()` / `count()`。
      tick が書き loop が読むので、2 面のバッファと番号 (seqlock) で組を揃える (割り込みは止めない)。
    - `EdgeRateEstimator`: ハードウェアに依存しない計算。tick ごとに (エッジ番号, 時刻) を履歴に入れ、回転を出す。
    - `EdgeSource` (`interfaces/sensor/edge_source.hpp`): ハードウェア。tick ごとに累計エッジ数と、1 つのエッジの (番号, 時刻) を返す。
      - `QuadTimerEdgeSource` (エンジン pin 14 = QuadTimer3 ch2、クラッチ後 pin 15 = ch3): ch2 / ch3 でエッジを数え、空いている ch0 / ch1 を
        IP バスクロック ÷64 (0.43 µs、16 bit で約 28 ms で一周) で自走させ、副入力 (SCS) に入力 2 / 3 を選んで CAPT にエッジ時刻を取る
        (RM §54.9 CTRL[SCS])。CAPT は IEF を消すまで最初の 1 回しか更新されない (RM §54.4.4) ので、tick ごとに
        「エッジ数を読む → IEF を消す → エッジ数を読む」を同じ値になるまで繰り返し、次の tick の CAPT を「その値 + 1 番目のエッジ」とする。
        上位はソフトで拡張し、tick 時の CNTR と Clock の時刻の対応で Clock の時間軸に直す。割り込みは使わない。
      - `FreqMeasureEdgeSource` (車輪 4 輪、FreqMeasureMulti): 周期の列からエッジ時刻を組み立てる。止まった後の最初のエッジだけ tick の時刻で
        位置を決め (誤差 1 ms 以内)、あとは周期を足す (FlexPWM と PIT は同じ水晶由来でずれない)。両エッジは `ALTERNATE` (半周期を交互に出す)。
  - 計算 (`EdgeRateEstimator`):
    - 窓は「N 歯か最大時間 T の短いほう」。窓の始まりと終わりは同じ向きのエッジに揃える (両エッジ時はエッジ間隔を偶数にしてデューティ比の
      ずれを打ち消す)。1 歯の間隔が T より長い低回転では直近 1 歯分。1 歯分そろう前 (動き出し) は半歯の値を仮に出す。
    - エッジが来ない間は、直前の 1 歯の間隔より長くなった時点から「1 歯 ÷ 経過時間」を上限として値を下げる。`stale_ms` で 0、履歴を捨てる。
    - tick が止まった (flash 書き込みで約 150 ms) などでタイマーの一周を追えなくなったら、そのセンサーの履歴を捨てて数え直す。
    - rpm = hz ÷ 歯数 × 60。歯数はセンサーが持つ (使う側で換算しない)。
  - config (`Config.rotation`、`RotationConfig`): エンジン・クラッチ後・前輪・後輪 (左右共通) ごとに 歯数・窓の歯数 N・最大時間 T・
    止まり判定・両エッジ、車輪はタイヤ直径 (km/h 換算用、0x607 で使う予定)。`SensorCalib` の歯数 (14〜16) は `rotation` に移して廃止。
    既定: エンジン 12 歯 / N 24 (720°) / T 20 ms、クラッチ後 100 歯 / N 100 / T 10 ms、車輪 N = 1 回転 / T 20 ms、止まり 300 ms、両エッジ。
  - telemetry と SD ログは今どおりパルスの Hz (`hz()`)。
  - 成立確認: ベンチで空き PWM ピンを pin 14 / 15 にループバック (pin 9 → 14、pin 33 → 15。車輪の FlexPWM サブモジュールと
    モーターの PWM4 SM0 を避ける) し、周波数を振って精度と追従を見る。
- Launch Control の FSM (現在 50 ms) は、凍結解除時に tick へ移す (本書のスコープ外、注記のみ)。

### 6.8 flash 保存 (Phase 2a)

flash (LittleFS) への書き込み中は割り込みが約 150 ms 禁止され、tick が止まる。モーター制御も止まるので、
**書き込みの直前に ETC を止め、終わったら再開する** (2026-10-11 決定)。対象は flash に書く 3 コマンド:
`save` (保存ボタン)、`set_config` (Config のインポート、反映してすぐ保存)、`format_fs` (FS のフォーマット)。

手順:
1. loop (コマンド処理) が「flash 書き込み要求」を立てる。
2. tick は要求を見たら、**シフターのパルス出力中でないこと**を待ってから (パルスが止まった tick の中で伸びないように)、
   ETC を止め (アーミング ⑤、PWM 0)、「準備完了」を立てる。
3. loop は準備完了を待ち (上限 1 s。超えたらコマンドを失敗で返す)、flash に書く。
4. 書き終えたら要求を下ろす。次の tick でアーミングの通常判定に戻り、条件を満たしていれば ETC を再開する (PID はリセット)。

書き込み中 (約 150 ms) の影響:
- スロットルはバネで閉じる。ピットやベンチでは問題ないが、走行中に保存するとスロットルが一瞬閉じる。
- 安全層・CAN 送受信・ログ採取も止まる。0x60A は約 150 ms 途切れる。CAN 受信のハードウェア FIFO は 6 フレームなので、
  あふれた分は失われる (0x741 は状態を 10 ms ごとに送り直すので、150 ms より長い押下は書き込み後に拾える)。
- `millis()` / `micros()` は SysTick 割り込みで進むので、止まっていた間の時間 (約 149 ms) を失い、以後ずっと遅れたままになる。
  そのためログの時刻は `micros()` ではなく PIT の Lifetime Timer から作り、止まっていた時間が穴として現れるようにする (§7.2)。

## 7. D: ギャップレスログ (Phase 3)

### 7.1 採取

- `tickISR` の最後 (§6.2 の 11) で `buildLogRecord()` を呼び、リングに書く。GPT2 の 1 ms 周期なので**等間隔**。
  同じ tick の判断結果 (アーミング・シフター) がそのレコードに入る。

### 7.2 レコードと時刻 (v5)

- `t_ms` (`millis()`) を **`t_us`** (µs、`uint32_t`) に置き換える。レコードは 148 B のまま。
- 時刻源は **PIT の Lifetime Timer** (チャネル 0 と 1 をつないだ 64 bit のダウンカウンタ、24 MHz。i.MX RT1060 RM §53.4.5)。
  - `micros()` は SysTick 割り込みでミリ秒を足すので、flash 保存のように割り込みが長く止まるとその時間を失う (§6.8)。
    PIT はハードウェアで数え続けるので失わない。64 bit なので一周しない (約 24,000 年)。一周を追う状態も要らない。
  - DWT サイクルカウンタ (`ARM_DWT_CYCCNT`) は使わない。デバッグ回路にあり、ソフトリセット (AIRCR.SYSRESETREQ = Local reset、
    SoC では COLD reset) ではリセットされない (ARMv7-M ARM §B1.5.1 / §C1.4、RM §21.3.4.2.2 "IOMUXC, SJC and debug are not reset")。
    書き込み後の再起動や `reboot` コマンドでは前の値から数え続け、時刻の原点が 0〜7 s ずれた。PIT は COLD reset でもリセットされる
    (RM Table 21-3 の Functional modules) ので、`SystemClock` のコンストラクタ (static 初期化) で動かした時点が毎回 0 になる。
  - 読み出しは `LTMR64H` → `LTMR64L` の順 (H を読むと L が保持される、RM §53.6.4)。間に別の ISR が H を読むと保持された L が
    上書きされるので、2 回の読み出しの間だけ割り込みを止める。
  - PIT のチャネル 0 / 1 を占有する。`IntervalTimer` (サンプリング ISR) は `TCTRL == 0` の空きチャネルを 0 から探すので、
    `SystemClock` (static 初期化) が先に動かすので、`IntervalTimer::begin()` (setup の中) はチャネル 2 を使う。
  - 読み取り口はインターフェース `Clock` (`interfaces/util/clock.hpp`: `nowUs64` と、そこから出す `nowUs` / `nowMs`)。
    いずれも読んだ瞬間の時刻を返す。実装は `SystemClock` (`util/system_clock.hpp`) で、状態は持たない。PIT の Lifetime Timer を
    触るのは `SystemClock` だけ。tick は先頭で 1 回読み、その tick の間はその値を部品に配る。
    実体 `sysClock` は `main.cpp` に 1 つだけ置き (extern 宣言は置かない)、部品へは時刻を引数で渡すか、`Clock` の参照を渡す
    (`DebugLogger` は `initialize()`、`StopWatch` を使う部品はコンストラクタ)。
  - 短い区間の計測や待ちのタイムアウトは `StopWatch` (`util/stop_watch.hpp`)。`Clock` を受け取り `nowUs64()` の差で測る。
- `t_us` は 2^32 µs = **71.6 分で一周**する。レコードには 32 bit のまま入れ、デコーダが「前のレコードより小さくなったら
  上位桁を 1 足す」で 64 bit に戻す。レコードは 1 ms 間隔で並ぶので取り違えは起きない。ファーム内の差分計算は
  符号なしの引き算なので一周しても正しい。
- `LogHeader` v5 (各ファイルの先頭、32 B): `magic[8]`、`version`=5、`record_size`、`log_hz`、予約、
  `dir_number` (起動ごとの通し番号)、`file_index` (ディレクトリ内の連番)、`rtc_epoch`。
- `SENSOR_LOG_VERSION` を 5 に上げ、`tools/decode_log.py` はディレクトリ単位でファイルを連結して読む。

### 7.3 リングバッファ

- `DMAMEM` に 2048 レコード (≈2 s、303 KB)。RAM2 空き約 500 KB に収まる。
- 満杯時は新しいレコードを捨てて `log_drops` を数える (順序を保つ。Q6)。
- 必要量の見込み: カード内部の待ち 最大 0.5 s、ファイル切り替え・古いファイルの削除 (exFAT なら数 ms〜数十 ms) を合わせても 1 s 未満。

### 7.4 カードとファイルシステム

- **exFAT** でフォーマットする。空き管理がビットマップ (1 クラスタ 1 bit) なので、確保と削除が FAT32 より約 30 倍速く、
  連続ファイルはクラスタの連結 (FAT チェーン) を書かずに済む。
- クラスタ境界をカードの消去単位にそろえてフォーマットする (カード内部の待ちが減る)。公式の SD Card Formatter は
  32 GB 以下のカードを FAT32 にし、OS 標準のフォーマットは境界がそろう保証がない。そこで **Teensy 自身が SdFat の
  `ExFatFormatter` (SD 規格どおりに境界をそろえる) でフォーマットする**。console にフォーマットのコマンドを追加し、
  確認ダイアログを出したうえで、ETC 停止中のみ実行できる。実行前にログ書き込みを止める。どの PC でもブラウザだけで同じ結果になる。
- FAT32 のカードが挿されても動作はする (SdFat は両対応)。ただし確保が遅く、§7.6 の終端判定が使えない (§7.6 末尾)。
- 推奨カード: 高耐久 (High Endurance) または産業用で、A1 / A2 か V30 の表示があるもの。

### 7.5 ディレクトリとファイル

- 起動ごとに 1 ディレクトリ: `dc_NNNN_YYYYMMDD_HHMMSS/` (RTC 未設定なら `dc_NNNN/`)。`NNNN` は起動時にルートを走査した
  最大番号 + 1。日時ではなく番号で新旧を決める (RTC 設定あり / なしの起動が混ざっても順序が崩れない)。
- その中に `0000.bin`, `0001.bin`, … を **100 MB (100,000,000 B) ずつ**作る。各ファイルは作成時に `preAllocate()` で
  連続領域を確保し、先頭にヘッダ、続けてレコードを書く (レコードはファイルをまたがない)。1 ファイル ≈ 67.5 万レコード ≈ 11.3 分。
- **ファイルが埋まったら** (Q3 決定: a): loop がそのファイルを `sync()` して閉じ、次のファイルを作って確保する。その間 loop は
  止まるが、リングが吸収する。
- **カードが満杯で確保できなかったら**: 一番古いファイル (最小の `NNNN` の最小の番号) を削除し、空になったディレクトリも削除して、
  確保をやり直す。今回の起動のファイルしか残っていなければ、今回の起動の古いファイルから消す。書き込み中のファイルは消さない。
- **書き込み・`sync()`・確保の失敗**: その起動ではログを止める (`active = false`)。失敗した書き込みを毎周やり直して loop が
  止まる事象 (§2.2) を防ぐ。**通知はしない** (2026-10-11 決定)。止まった後のレコードは `log_drops` に数えられ、telemetry の
  `SysStats` で見える。

### 7.6 書き出しと終端の判定

- loop の `SensorLogger::service()` がリングから 4 KB 以上たまったら最大 8 KB をまとめて `write()` (Q7)。
- `sync()` は 2 s ごと (Q8)。exFAT の事前確保ファイルは「確保済みの長さ (DataLength)」と「有効な長さ (ValidDataLength)」を
  別に持つ。`preAllocate()` は前者だけを 100 MB にし、後者は `sync()` のたびにディレクトリへ書かれる。
  **有効な長さより後ろは、PC からはゼロとして読める** (SdFat の `fileSize()` も有効な長さを返す)。
- そのため、以前消したファイルの古いデータが確保領域に残っていても、PC やデコーダからは見えない。終端は
  「全バイトがゼロのレコード」で判定できるので、起動ごとの識別子 (旧案) は不要。
- 電源断で失うのは、最後の `sync()` 以降に書いた分 (最大 2 s) と、リングに残っていた分。
- FAT32 カードの場合は有効な長さの仕組みがないので、末尾に古いデータが見えうる。デコーダは `t_us` の連続性
  (前のレコードから 0〜1 s 後でなければ終端) でも止める。

### 7.7 検証

- 30 分のログ中にポート開閉・コマンド送信・`set_config` (flash 保存) を混ぜても、`t_us` の差分が全て 1000 µs ± ジッタ、
  `log_drops == 0`。ただし flash 保存の間 (約 150 ms) はレコードが欠ける (§6.8)。
- 100 MB のファイル切り替えをまたいで連続していること (ファイル境界で欠けがない)。
- `t_us` の一周 (起動から 71.6 分) をまたいでデコーダが連続した時刻を出すこと。
- 小容量のカード (または確保サイズを一時的に小さくしたビルド) で満杯にし、一番古いファイルが消えて記録が続くこと。
- ストール注入 (§8) 500 ms → 欠け 0 / drops 0。3 s → drops が計上され、その後も記録が続く。

## 8. 検証用デバッグコマンド (ベンチ専用)

「ストール注入」= SD や USB の停止を人工的に再現する**ベンチ専用コマンド**。
`DebugStallCmd { uint32 ms }` を追加し、loop 内で `delay(ms)` する (**`-DBENCH_DEBUG` ビルドのみ
登録**、本番は ok=false。Q10)。これで次を実機で確認できる:

- Phase 2: 200 ms ストール中に SIG_IN を落とすと ETC が 1 ms 以内に止まる。CAN の送信間隔が乱れない。
  0x741 のシフト入力に対するパルス開始と、パルス幅がストールの影響を受けない (SD ログで確認)。
- Phase 3: ストール中も記録が途切れない。

## 9. 実装タッチポイント

| ファイル | Phase | 変更 |
|---|---|---|
| `src/util/gpt_timer.{hpp,cpp}` | 2a | 新規: GPT2 タイマー |
| `src/main.cpp` | 2a / 3 | `motorControlISR` を廃止しモーター制御を `tickISR` (GPT2) へ、loop を SD / telemetry / コマンド / イベント送信だけに |
| `src/can/can_bus.{hpp,cpp}`、`can_controller.{hpp,cpp}` | 2a | 受信割り込み + `onReceive`、優先度 64、`poll()` 削除、送信 33 ms |
| `src/constants.hpp` | 2a | `CAN_TX_INTERVAL_MS` 33、優先度定数 |
| `src/control/control_input.{hpp,cpp}` | 2a | override の書き込みを割り込み禁止区間に |
| `src/shift/auto_shifter.{hpp,cpp}` | 2a | `onPaddles()` (CAN 受信から即時) と `tick()` (1 ms) に分割、最短間隔 0.5 s、debug は ISR から `DebugLogger::log` |
| `src/etc/experiment_runner.{hpp,cpp}` | 2a | 開始 / 停止の要求フラグ、debug は ISR から `DebugLogger::log` |
| `src/etc/plausibility_validator.{hpp,cpp}` | 2a | `validLatched() const` (loop 側の読み出し用) |
| `src/util/loop_stats.{hpp,cpp}` | 2a | tick / CAN 受信 ISR の所要時間 |
| `src/sensor/iam20680hp.{hpp,cpp}`、`sensor_hub.{hpp,cpp}` | 2b | SPI1 の DMA、移動平均、`readImu()` 削除 |
| `src/sensor/{edge_rate_estimator,edge_rotation_sensor,quadtimer_edge_source,freqmeasure_edge_source}.{hpp,cpp}`、`interfaces/sensor/`、`sensor_hub.{hpp,cpp}` | 2a / 2c | 毎 ms 更新 (2a)、エッジ番号と時刻による周期計測・可変窓・RotationConfig (2c) |
| `src/commands/command_controller.cpp`、`spec/proto/drive_controller.proto`、`dc-console/` | 3 | SD フォーマットのコマンド (確認ダイアログ、ETC 停止中のみ) |
| `src/util/log/sd_binary_writer.{hpp,cpp}` | 3 | `SdFs` / `FsFile`、ディレクトリ・100 MB 事前確保・切り替え・古いファイルの削除・失敗時停止 |
| `src/util/log/sensor_logger.{hpp,cpp}` | 3 | リング (ISR から採取)、まとめ書き出し、drops |
| `src/util/log/log_record.hpp`、`src/log_record_builder.cpp` | 3 | v5 (`t_us`、ヘッダ) |
| `tools/decode_log.py` | 3 | v5、ディレクトリ単位の連結、`t_us` の一周補正、終端判定 |
| `spec/proto/drive_controller.proto` | 2a | `SysStats` に tick / CAN 受信 ISR の最大、`DebugStallCmd` → 両側再生成 |
| `src/commands/command_controller.cpp` | 2a | `DebugStallCmd` (BENCH_DEBUG)、実験は要求フラグ経由 |
| `src/configurator.{hpp,cpp}`、`src/util/flash.{hpp,cpp}` | 2a | flash 書き込みの前後で要求 / 準備完了の受け渡し (§6.8) |
| `dc-console/` | 2a | SysStats の表示項目追加 |
| `CLAUDE.md` | 2 / 3 | 並行性節 (コンテキストと優先度の表)、CAN 節 (受信割り込み・送信周期)、SD ロギング節 |

## 10. 決定事項

### 10.1 2026-10-06 (Phase 1 時点。改訂後も有効なもの)

| Q | 決定 |
|---|---|
| Q1 | console は open 直後に `setSignals` で DTR を明示 (RTS は触らない)。ファーム側の DTR ゲートは撤回し、全フレームの送信ガードに置換 (§4) |
| Q2 | `SysStats` は 50 Hz の State に同梱 |
| Q3 | console 表示は読み出し 1 行 |
| Q5 | リング 2048 レコード (≈2 s) を RAM2 に |
| Q6 | 溢れ時は新規を捨てて `log_drops` を数える |
| Q7 | 4 KB たまったら最大 8 KB をまとめ書き (実測で調整) |
| Q8 | `sync()` は 2 s ごと |
| Q10 | ストール注入コマンドを `-DBENCH_DEBUG` ビルドにのみ登録 |
| Q12 | 既存の double 競合 (`manualTarget`、校正 slope / intercept) は**別課題**。競合は許容 |

旧 Q4 (3 本目の IntervalTimer で安全層 ISR)、Q9 (512 MB 1 ファイル)、Q11 (シフターを丸ごと安全層 ISR へ)、
Q13 (#5 からの stacked ブランチ) は 2026-10-11 の改訂で置き換え。

### 10.2 2026-10-11 (改訂)

| 項目 | 決定 |
|---|---|
| 基盤 | ベアメタル (Arduino / Teensy コア) のまま。RTOS (Zephyr 等) は見送り |
| 方針 | 時間の決まった処理はすべて割り込み。loop には遅れてよい処理 (console 送信・コマンド・SD 書き出し) だけ |
| IMU | 割り込み + DMA で 8 kHz、移動平均 |
| CAN | 受信は割り込み (即時)。送信は等間隔で、周期は kart-can の定義 (`can.yaml` の `cycle_ms` → `KART_CAN_*_CYCLE_TIME_MS`) をそのまま使う: 0x600〜0x603 は 16 ms、0x60A は 33 ms、0x604〜0x609 (未実装) は 20 ms。周期を変えるときは kart-can を先に変える (当初の「30 Hz」は kart-can の設計を確認する前の決定で、取り消し) |
| パルスカウンタ | 等間隔 (100 ms) で割り込みから更新 |
| シフター | 入力は CAN 0x741 の受信で即時処理。チャタリング除去は送信側。パルス出力の最短間隔 0.5 s |
| ログの時刻 | µs で記録。一周はデコーダで補正。時刻源は PIT の Lifetime Timer (64 bit。旧 Q-G。`micros()` は割り込みが長く止まると時間を失い、DWT はソフトリセットで 0 に戻らないため) |
| SD | exFAT。起動ごとに 1 ディレクトリ、100 MB の事前確保ファイルを順に作る。満杯なら一番古いファイルを消す |
| 失敗時 | ログを止めるだけ。通知はしない |
| 仕様書 | 本書 (Phase 2 / 3) にまとめる |
| シフトの最短間隔 (旧 Q-B) | パルス**開始**から次のパルス開始まで 0.5 s。間隔内の入力は捨てる。`cooldown_ms` を置き換え、既定 500 |
| IMU の平均窓 (旧 Q-C) | 8 サンプル (= 1 ms 分) |
| 同定実験 (旧 Q-D) | 進行を tick に移す (§6.2 の 7)。開始 / 停止は loop からの要求フラグ |
| exFAT のフォーマット (旧 Q-E) | Teensy が `ExFatFormatter` で行う。console のコマンドから、確認ダイアログ付き・ETC 停止中のみ |
| 1 ms 割り込みの名前 (旧 Q-A) | `tickISR`。既存の `sensorSamplingISR` と同じ書き方。安全層を含むことはコメントと §6.2 の表で示す |
| モーター制御 | `tickISR` に統合 (旧 `motorControlISR` と GPT1 は使わない)。安全層の直後に回す |
| flash 保存 | 書き込みの直前に ETC を止め、終わったら再開 (§6.8)。シフターのパルス中は待つ |
| 回転計測の平均歯数 (旧 Q-F) | 定数 (`constants.hpp`)。エンジン 12、クラッチ後 10 |
| 回転計測 | 毎 ms 更新。6 センサーとも (エッジ番号, 時刻) から可変窓 (N 歯か T の短いほう)・両エッジで周期計測 (2c、§6.7)。設定は config RotationConfig。ピンは変えない |

## 11. 未決事項

なし (2026-10-11 時点)。
