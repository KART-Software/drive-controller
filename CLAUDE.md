# CLAUDE.md

このファイルは Claude Code (claude.ai/code) がこのリポジトリで作業する際のガイドです。

## リポジトリ構成

2 つの連携サブプロジェクトが `spec/proto/` の Protobuf スキーマを共有しています:

- `dc-firmware/` — Teensy 4.1 ファームウェア (PlatformIO + Arduino framework)。ETC (電子スロットル)、オートシフター、Launch Control (現在凍結)、センサーサンプリング、CAN、シリアルプロトコルを担当。
- `dc-console/` — Preact + Vite の Web アプリ。Web Serial API 経由でファームウェアと USB シリアル通信し、ライブモニタリング・キャリブレーションを行う。
- `spec/proto/drive_controller.proto` — ホスト ↔ デバイス間ワイヤフォーマットおよび永続化 `Config` の単一ソース。

proto のワイヤフォーマットは `COBS( protobuf_bytes ‖ crc16_le ) 0x00` — `dc-firmware/src/serial/` および `dc-console/src/protocol.ts` を参照。

## ビルド / 開発コマンド

### ファームウェア (PlatformIO)

```bash
# ビルド
pio run -d dc-firmware -e teensy41

# Teensy にアップロード
pio run -d dc-firmware -e teensy41 -t upload

# シリアルモニター (115200 baud)
pio device monitor -d dc-firmware
```

初回 clone 後は `git submodule update --init --recursive` で `FlexCAN_T4` を取得すること。

`dc-firmware/pre_build.py` がビルド前に自動で動き、(1) clangd 用の `compile_commands.json` を再生成、(2) nanopb 経由で `spec/proto/` から `dc-firmware/src/proto/drive_controller.pb.{c,h}` を再生成する。**生成された `.pb.{c,h}` を手で編集しないこと。**

### コンソール (pnpm)

```bash
cd dc-console
pnpm install
pnpm dev                    # vite 開発サーバー
pnpm build                  # tsc --noEmit && vite build → dist/
pnpm run gen:proto          # buf 経由で src/proto/drive_controller_pb.ts を再生成
```

`spec/proto/drive_controller.proto` を編集したら **両側** で再生成が必要: ファームウェアは再ビルド (pre_build が nanopb を走らせる)、`dc-console/` では `pnpm run gen:proto`。

### clangd

ファームウェアは Arduino/Teensy ツールチェインを使うため、clangd には 2 つの補正が要る (`.clangd` + `dc-firmware/.vscode/settings.json`):

1. **システムインクルード**: `compile_commands.json` には newlib / libstdc++ の暗黙パスが入らないので、エディタ側から `--query-driver=${env:HOME}/.platformio/packages/toolchain-gccarmnoneeabi-teensy/bin/arm-none-eabi-*` を渡して g++ から取得する (VS Code 設定に記載済み。他エディタでも同じ引数が必須。無いと `<type_traits>` 等が見つからず大量の幻エラーになる)。
2. **型マクロの差**: GCC (arm-none-eabi) は `int32_t`/`uint32_t` を `long`/`unsigned long` にするが clang は `int`/`unsigned`。そのままだと Teensy コアの `int32_t random(void)` と newlib の `long random(void)` が衝突するので、`.clangd` で `-D__INT32_TYPE__=long` 等を与えて GCC に合わせている。

`-mcpu/-mfpu/-mfloat-abi/-mthumb` は clang がそのまま解釈できるので剥がさない (剥がすと multilib の選択がずれる)。`.clangd` に**絶対パスを書かないこと** (旧マシンのパスが残って preamble が壊れた前例あり)。ツールチェイン変更後に幻のエラーが出たら `compile_commands.json` を再生成 (`pio run` で自動) し、`clangd --query-driver=... --check=dc-firmware/src/main.cpp` で確認する。

## アーキテクチャ

### ファームウェア並行性 (Teensy 4.1)

`dc-firmware/src/main.cpp` は 4 つの並行コンテキストで動く (docs/loop_nonblocking_spec.md §3.1)。共有状態を触る前に自分のコードがどれに属するか把握すること。**時間の決まった処理はすべて割り込み、`loop()` には遅れてよい処理だけ**を置く:

1. **`tickISR`** — GPT2, 周期 1 ms, NVIC 優先度 **64**。毎回、SIG_IN 読み → `ControlInput::update()` → モード反映 → プラウシビリティとアーミング (`armEtc()`) → **モーター制御** (`motorController.cycle()`、ETC が ON のときだけ) → 同定実験 → オートシフター (`tick()`) → 回転計測 (毎 ms、エンジン / クラッチ後は直近 100 ms の窓) → CAN 送信 (0x600-0x603 は 16 ms、0x60A は 33 ms) の順。ETC の開始 / 停止 (`startEtc()` / `stopEtc()`) はここからのみ。所要は `LoopStats` の `safety_max_us` (約 10 µs)。
2. **`canRxISR`** — FlexCAN3 の受信 (FIFO) 割り込み, 優先度 **64**。`CanRxData` に取り込み、0x741 のシフト入力はその場で `autoShifter.onPaddles()` に渡す。tick と同じ優先度なので互いに割り込まず、シフター・`CanRxData`・`ControlInput` はこの 2 つの間で排他不要。FlexCAN の `events()` は呼ばない (呼ぶとコールバックがキュー経由になる)。
3. **センサーサンプリング ISR** — `IntervalTimer` (PIT のチャネル 2。0 / 1 は時刻の Lifetime Timer), 周期 62.5 µs, 優先度 **208** (USB より下)。既定 (`-DADC_DMA`) では ADS8688 を **DMA (非ブロッキング SPI)** で駆動し、ISR は前回 DMA 結果を移動平均に反映して次の転送を kick するだけ (`sensor_hub.sampleAdcDmaIsr()`)。`-DADC_DMA` を外すとブロッキング読み (`sensor_hub.read()`) を `loop()` で行うフォールバックになる。
4. **`loop()`** — 非 ISR: IMU 読み (Phase 2b で DMA 化予定)、telemetry (50 Hz)、コマンドディスパッチ、SD ロギング (1kHz, カード挿入時)、ISR が積んだ debug の送信、launch FSM tick (凍結時はビルドから除外)。シリアル送信は全フレームが TX リングの空きを確認し、足りなければ捨てる (loop をブロックさせない, `serial_protocol.cpp`)。50 Hz telemetry は TX バッファ 1 本分 (2 KB) を残して先に捨てるので、コマンド応答と debug は最後まで届く。

**時刻は `Clock` で、`millis()` / `micros()` は使わない。** `Clock` はインターフェース (`interfaces/util/clock.hpp`)、実装は `SystemClock` (`util/system_clock.hpp`)。時刻源は PIT の Lifetime Timer (64 bit、24 MHz。ソフトリセットでも 0 に戻る。DWT はソフトリセットで戻らないので使わない)。`SystemClock` は状態を持たず、コンストラクタで PIT を動かす (その時点が時刻の 0。static 初期化なので `IntervalTimer` より前)。`Clock` は読んだ瞬間の時刻 (`nowUs64()` / `nowUs()` / `nowMs()`) だけを返す。tick は先頭で 1 回読んで部品に配る。実体 `sysClock` は `main.cpp` だけが持ち (extern しない)、部品へは時刻を引数で渡すか `Clock` の参照を渡す (`DebugLogger::initialize()`、`StopWatch` を使う部品のコンストラクタ)。区間の計測やタイムアウトは `StopWatch` (`util/stop_watch.hpp`)。時刻はクラスのメンバーにしまわず、使う関数まで引数で渡す (`nowMs_` を持つと、別の文脈が刻んだ時刻のほうが新しくなって差が負になる)。別の文脈 (CAN 受信割り込みなど) が刻んだ時刻との差は符号付きで扱い、新しいものは経過 0 とみなす。`src/interfaces/` はインターフェースを置く場所で、中は `src/` と同じ構成にする。

**ISR (tick / CAN 受信) から `Serial` / SD / flash / `malloc` / `delay` を呼ばないこと。** debug はどこからでも `DebugLogger::log()`。`DebugLogger` は時刻を付けて出力先 (`LogWriter`、`interfaces/util/log/`) に配るだけで、その場で出せるかは出力先が判断する。USB へ送る `SerialDebugWriter` は ISR の中や割り込み禁止中、USB がつながっていない間 (起動時に接続は待たない) はキューに積み、loop の `DebugLogger::service()` (→ 各 writer の `service()`) が送る。ISR から呼ぶときの書式は整数と文字列だけ (newlib の浮動小数点書式は malloc する)。loop から tick の状態を変えるときは、要求を置いて tick に処理させる (例: `ExperimentRunner::requestStart()`) か、短い `noInterrupts()` 区間で書く (例: `ControlInput::setOverride()`)。

**flash (LittleFS) への書き込み中は割り込みが約 150 ms 止まる。** `Flash` が `FlashWriteGate` で tick に知らせ、tick は ETC を止めてから (シフトのパルス中は待つ) 書き込みを許可し、終われば再開する (§6.8)。書き込みは必ず `Flash` 経由で行うこと。

ISR と loop の間で共有される可変状態は **必ず保護する**。`MovingAverage<N>` は既に `sum` の読み取りを `noInterrupts()` で守っている — このパターンを踏襲すること。センサーの `update()` を呼ぶのは ISR のみで、読み手は移動平均経由で十分整合した値を見る。

### センサー / ターゲットの所有

- `SensorHub` がすべてのセンサーと `EtcTarget` を所有する。多くのコードは `SensorHub&` を `const` で受け取り、`Configurator` だけが `SensorHub::mut` 経由で mutable アクセサに到達する (意図的な friend-by-API)。
- `EtcTarget` がターゲットスロットル開度を決める: APPS と ITTR (IST コントローラからの CAN コマンド) の切り替え、モード (Normal / Restricted / Calibration / MotorOff)、アイドリングおよびモード別キャップ、オプションの多項式ターゲットカーブ。

### 回転計測 (車輪速 / エンジン / クラッチ後)

6 つの回転センサーを同じ形で扱う (`docs/loop_nonblocking_spec.md` §6.7)。`SensorHub` が所有し、`tickISR` が `updatePulse(nowUs)` を毎 ms 呼ぶ。

- **使う側の口**: `RotationSensor` (`interfaces/sensor/rotation_sensor.hpp`)。`sample()` で `RotationSample { hz, rpm, timeUs (窓の中心), windowUs, edges }`、`hz()` / `rpm()` / `count()`。使う側 (`AutoShifter`・`StarterController`・`LaunchController`・telemetry・ログ) はハードウェアを知らず、歯数の換算もしない (rpm はセンサーが出す)。loop からも組の揃った値が読める (seqlock)。**tickISR より優先度の高い割り込みから `sample()` / `hz()` / `rpm()` を呼ばないこと** (tick の書き込み途中に割り込むと読み直しが終わらない)。
- **計算**: `EdgeRateEstimator` (ハードウェア非依存)。(エッジ番号, 時刻) の履歴から、窓 = 「N 歯か最大時間 T の短いほう」で回転を出す。両端は同じ向きのエッジに揃える (両エッジ時のデューティ比のずれを打ち消す)。エッジが来ない間は「1 歯 ÷ 経過時間」を上限に下げ、`stale_ms` で 0。
- **ハードウェア** (`EdgeSource`、`interfaces/sensor/edge_source.hpp`):
  - **エンジン (pin 14) / クラッチ後 (pin 15)** — `QuadTimerEdgeSource`。QuadTimer3 ch2 / ch3 でエッジを数え、空きの ch0 / ch1 を IP バス ÷64 で自走させて副入力でエッジ時刻をキャプチャ (割り込みなし)。入力選択 (daisy) を pin 14 / 15 のパッドに設定すること (リセット値は別パッド)。ピン 18 / 19 は `analogWrite` に使えない。
  - **車輪 FL/FR/RL/RR** — `FreqMeasureEdgeSource`。**FreqMeasureMulti** (FlexPWM 入力キャプチャ) の周期の列からエッジ時刻を組み立てる。各輪は **別々の FlexPWM サブモジュール**のピンに割り当てること (同一サブモジュールの 2 ピンは同時計測不可)。
- **config**: `Config.rotation` (`RotationConfig`): エンジン・クラッチ後・前輪・後輪 (左右共通) ごとに 歯数・窓の歯数・最大時間・止まり判定・両エッジ、車輪はタイヤ直径。歯数は以前の `SensorCalib` から移した。設定は `setParams()` で置き、setup では `SensorHub::begin()` (config の適用の後に呼ぶ) が、実行中は次の tick が取り込む。
- telemetry と SD ログはパルスの Hz (`hz()`)。`count()` は両エッジならエッジ数 (パルス数の 2 倍)。

### CAN

- `CanController` + `CanBus` (FlexCAN_T4 サブモジュール, **CAN3 = Teensy 4.1 の pin 30/31**。CAN1 の 22/23 はモーター PWM/DIR に割当済み)。CAN 定義の単一ソースは submodule `lib/kart-can` (`can.yaml` → 生成 `kart_can.h`、仕様は同リポジトリの `docs/can-spec.md`)。TX (`tickISR` から): IMU + ギア (`0x600-0x603`, 16 ms)、状態 `DC_Status (0x60A)` (33 ms: 適用中の ETC モード・オートシフト・Shutdown 回路)。**送信周期は kart-can が生成する `KART_CAN_*_CYCLE_TIME_MS` を使い、ファーム側で定数を持たない** (変えるときは kart-can の `can.yaml` を先に変える)。RX は受信割り込み (`canRxISR`)。RX: `Control (0x740)` (data_logger、33 ms: ETC モード / launch / auto-shift / starter) と `Shift (0x741)` (10 ms: シフトパドル上下)。
- 途絶時のフォールバックは `ControlInput::update()` が持つ (`CanRxData` は受信した値と時刻だけを持つ)。Control が 200 ms 途絶したら mode を NORMAL に、launch / auto-shift / starter を false にフォールバック。**`MOTOR_OFF` はラッチ状態であり、CAN 断で自動復帰させない。** 自動復帰パスを追加しないこと。Shift は 100 ms 途絶で両パドル 0。起動後に一度も受信していない間は途絶扱いにしない。
- 制御フレームの byte0 で `UNSPECIFIED (0)` や未知値を受信した場合は **現在モードを維持** し、`lastControlFrameMs` のみ更新する (ハートビート扱い、意図的設計)。
- **制御入力は `ControlInput` (`src/control/`) が唯一の入口** (docs/control_input_spec.md)。消費側 (main のモード反映・アーミング③、`AutoShifter`、`ExperimentRunner`、ログ、telemetry) は入力元を知らない。ETC モードは (1) console の上書き → (2) 0x740 を起動後に一度も受信していなければ **MOTOR_OFF で待つ** → (3) CAN の受信値、の順で決まる。console の上書き (`SetControlOverrideCmd`) はベンチ用で RAM のみ。**0x740 を受信している間は拒否し、受信が始まったら自動解除**する (車載時にドライバーのスイッチを上書きしないため)。GPIO のモードノブ / auto-shift スイッチ / シフトパドル入力は廃止した。

### オートシフター

`shift::AutoShifter` (`dc-firmware/src/shift/`) — クイックシフター/シーケンシャル
ミッションの UP/DOWN シフト信号を制御する。設計仕様は `dc-firmware/auto_shifter_spec.md` が一次ソース。

- ON/OFF とシフトパドルは `ControlInput` から `tick({.autoOn, .upPressed, .downPressed}, nowUs)` で渡される (入手元は CAN 0x740 byte2 / 0x741、または console の上書き)。0x741 は受信割り込みから `onPaddles()` で**即時**に渡され、その場でパルスを始める (開始時刻は受信した瞬間の `sysClock.nowUs()`、チャタリング除去は送信側の責務)。OFF=manual (ドライバー判断)、ON=auto (独自ロジック)。パドルは押下状態で渡され、立ち上がり検出は `AutoShifter` が行う。
- 出力は **両モードとも「エッジ検出 → 整形パルス」** (`Idle → Pulsing → Cooldown`)。OFF はレベルミラーではない。パルスの開始から次の開始までは最短 0.5 s (`SHIFT_MIN_INTERVAL_MS`、config `cooldown_ms` が長ければそちら)。その間の要求は捨てる。
- パルス幅は transmission/ギア/方向/停車状態で決まる (config 化): IST=単一幅、NORMAL=走行中100ms (N スキップ)・停車中1速UP/2速DOWN 25ms (N 入れ)。
- ON 走行ロジックは RPM + 車輪速 + ギア + **APPS スロットルゲート** (アクセルオンで UP / オフで DOWN → ハンチング根治)。ダウンは速度ゲートなし。
- 4 層構成: 判断ロジック (`evaluate`, 将来の拡張点) / 調停 / 出力整形 / I/O。
- `SensorHub` 全体ではなく必要センサー (engine/wheelFL/wheelFR/gps/apps1) だけを const 参照で受け取る。`tick()` は `tickISR` から毎 ms 呼ばれ、オートシフトの ON/OFF だけで判定する (ETC プラウシビリティには非依存。auto-shift の安全フォールバックは CAN 断→manual)。
- シフト機構制御 (クラッチ/点火カット/ブリッピング/オーバーレブ保護) は IST コントローラ責務。

### セルモーターリレー

`starter::StarterController` (`dc-firmware/src/starter/`、仕様は `docs/starter_spec.md`) — セルスイッチ (CAN 0x740 byte3、`ControlInput::starterPressed()`) でセルモーターのリレー (`STARTER_RELAY_PIN`、暫定ピン 6) を駆動する。`tickISR` から毎 ms `tick()`。

- 押した瞬間 (0 → 1) に ON。エンジン回転がしきい値を超えていれば ON にしない (押し直すまで待つ)。
- 回転がしきい値を超えた状態が `running_confirm_ms` 続いたら OFF (エンジンがかかった)。
- ON にしてから `min_hold_ms` はスイッチや CAN 途絶に関係なく回し、それ以降はスイッチが 0 (途絶を含む) で OFF。押し続ける限り上限なし。
- しきい値と時間は config (`StarterConfig`、既定 2000 rpm / 1000 ms / 5000 ms)。安全条件 (①②③) とは無関係。0x60A の `starter_relay` と SD ログの flags (bit 6 リレー / bit 7 スイッチ) に載る。telemetry には載せない。

### Launch Control (現在凍結)

**`-DLAUNCH_CONTROL_ENABLED` が未定義の通常ビルドでは凍結中。** `main.cpp` の `launchController.begin()` と FSM tick が `#if defined(LAUNCH_CONTROL_ENABLED)` でガードされ、`update()` が呼ばれないため FSM は Idle のまま (clutch motor 停止)。インスタンスと参照はビルドに残るので、フラグ ON ビルド (CI 等) で腐敗検知できる。制御フレーム `CONTROL (0x740)` の launch ビットは受信し続けるが何も起きない。

`launch::LaunchController` は FSM `Idle → Ready → Approach → EngageControl → FullEngage → Idle`。制御量は **エンゲージ率** (`wheel_rps_at_engine / engine_rps`) で、外側 PID がクラッチ位置指令を出す。内側 PID は `ClutchMotor` 内でクラッチセンサに対するクローズドループを構成する想定 — proto/config 上は配線済みだがアクチュエータ側はハード待ち (TODO.md A-2 参照)。

`BitePointEstimator` は Approach 中にクラッチミートポイントを学習する (走行毎に EMA)。`BitePointFile` は `Config` とは別に flash に保存される (`/bite_point.pb`)。有効時の `update()` は `LAUNCH_UPDATE_INTERVAL_MS` (50ms, 20Hz) の独立タイミングで呼ばれる (CAN TX 周期から分離済み)。

### 設定 / Flash 永続化

`Configurator` が LittleFS バックエンドの flash (`Flash` → `LittleFS_Program`, ファイル `/config.pb`) への書き込みを単独で所有する。

重要な不変条件: 部分 `dc_Config` が来たとき (例: `SetConfigCmd` で 1 セクションだけ編集) のマージは **必ず** `overlayConfig()` を経由すること。これはトップレベルサブメッセージごとに `has_*` を確認し、ゼロ値サブメッセージをスキップする。`config = src` で直接代入すると無関係のキャリブレーション値を黙ってゼロで上書きしてしまう。`dc_Config` にトップレベルサブメッセージを追加した場合は `overlayConfig()` に 1 行追加する (該当箇所にコメントあり)。

`calibrate()` は一方向の適用ステップ (config → 実センサー / コントローラ)。構造変更後はユーザに通知する前に必ず呼ぶこと。

### SD ロギング

`SensorLogger` (`dc-firmware/src/util/log/`) が Teensy 4.1 内蔵 SD (SDIO, `BUILTIN_SDCARD`) へセンサーデータを **1kHz・固定長バイナリ**で記録する。**Config の flash (LittleFS) とは別系統**。

- 構造はテキスト用ロガーのバイナリ版で対になっている: `BinaryWriter` (interface) ↔ `LogWriter` (interface、`interfaces/util/log/`)、`SdBinaryWriter` (SD I/O) ↔ `SerialDebugWriter`、`SensorLogger` (facade) ↔ `DebugLogger`。フォーマット (`LogHeader` / `LogRecord`, v4 = 148B/レコード) は `util/log/log_record.hpp`。
- **カード挿入時のみ有効**・**電源投入毎に新ファイル**: `dc_log_YYYYMMDD_HHMMSS.bin` (RTC 設定時) / `dc_log_NNNN.bin` (未設定時)。RTC は `setSyncProvider(Teensy3Clock.get)` で起動時反映、`set_rtc` で更新。
- レコード生成は**純粋関数** `buildLogRecord()` (`src/log_record_builder.{hpp,cpp}`) に集約 — 全入力を const 参照で受け、`SensorHub` 等の public const getter から読むだけで副作用なし。`SensorHub` はログ形式に依存しない。plausibility の valid/errors は `PlausibilityValidator::currentlyValid()` / `errorBits()` (副作用なし const) で読む。
- 収集・書き込みは `loop()` (1kHz tick + 毎ループ `service()` で定期 flush)。SD 書き込みストール中は loop が止まりその間サンプルが空く (レアな小ギャップ)。ISR (tick/CAN 受信/sampling) は影響を受けない。停止時間は `LoopStats` で計測し telemetry (`SysStats`) と LogRecord v4 に載る。完全ギャップレス化 (リングバッファ + ISR 採取) は `docs/loop_nonblocking_spec.md` §7 (Phase 3)。
- フォーマット変更時は `SENSOR_LOG_VERSION` を上げ、デコーダ `tools/decode_log.py` (.bin → CSV) を合わせること。
- ホスト向けテレメトリの proto (`dc_Sensor`) とは別物: proto = 自己記述・可変長・50–60Hz、SD ログ = 高レート固定バイナリ (memcpy のみ)。

### コマンドルーティング

ホストからのコマンドは protobuf `Command` の `oneof body`。`CommandRouter` が oneof タグ (`which_body`) でハンドラを振り分け、ハンドラは `CommandController::registerCommands()` で登録する。新コマンド追加手順: proto を拡張 → 両側で再生成 → `CommandController` にハンドラを追加 → `router.on(dc_Command_<field>_tag, ...)` で登録。

### プラウシビリティ / エラーハンドリング

`PlausibilityValidator` は `tickISR` で毎 ms チェックされる (評価する `isValid()` は tick からのみ呼ぶ。loop は副作用なしの `validLatched()` / `currentlyValid()` を読む)。ETC のアーミング (モーター ON/OFF) は tick の **SHUTDOWN 安全層** (`armEtc()`) が一元管理する。ETC を止める要因は独立に 5 つ (④ 同定実験のコースト、⑤ flash 書き込み中を含む。主なものは次の 3 つ):

- **① プラウシビリティ違反** → ETC 停止 + `SHUTDOWN_RELAY_PIN`=LOW (点火系 SHUTDOWN 回路を遮断、**復帰不可ラッチ**)。
- **② `SHUTDOWN_SIG_IN_PIN`=LOW** (外部 AND 回路が開いた) → ETC 停止 (RELAY は HIGH 維持、SIG が HIGH に戻れば**自動再開**)。
- **③ `MOTOR_OFF` モード** (CAN 0x740 由来、または 0x740 未受信の待機、または console の上書き) → ETC 停止 (RELAY 維持)。別モードを受信 / 上書きすれば再開。CAN 途絶時は MOTOR_OFF を保持する。

ETC 停止 = `setMotorOff()` (PWM=0 + モーター電源リレー OFF)、再開 = `setMotorOn()` (内部で `pid.reset()`)。モーター制御は tick の中で「ON なら `cycle()`」なので、タイマーの開始・停止はない。① は復帰不可で電源再投入が必要。詳細ロジックは `main.cpp` の「ETC アーミング」コメントが一次ソース。**`SHUTDOWN_RELAY_PIN`(点火系) を落とすのは①だけ**で、`DcMotor` が持つモーター電源リレー `DC_MOTOR_RELAY_PIN` とは別系統。(燃料ポンプ制御は削除済み — 燃料カットはエンジン ECU 責務。)

### ピンアサイン

`dc-firmware/src/constants.hpp` で `#define` によりモータードライバを選択 (現在は `G2_18V17`)。主要ピンは実配線に合わせて確定済み: モーター(G2)=SLP21/PWM22/DIR23/FLT20、車輪速=24/25/28/36 (FlexPWM, 別サブモジュール)、RPM=Engine14/クラッチ後15 (QuadTimer)、オートシフター出力=UP_OUT4/DOWN_OUT5 (パドル入力は CAN 0x741)、CAN=CAN3(30/31)、ADC(SPI0)=10-13、IMU(SPI1)=0/1/26/27。パルスカウントのピンはペリフェラル制約あり (上記「パルスカウント」節)。ベンチテスト前に実配線と必ず照合すること。

## 規約

- C++ namespace はサブシステム単位: `etc::` (スロットル), `launch::` (Launch Control), `shift::` (オートシフター)。センサーと共通ユーティリティはグローバル。
- コメントは日本語の箇所が多い。新規コードは英語でも可だが、修正時は周囲のファイルに合わせる。
- debug メッセージは `DebugLogger::log` で出す (ISR からも同じ。出力先は登録した `LogWriter`。今は `SerialDebugWriter` が COBS フレームで送る)。`Serial` に直接出力しないこと (素の `Serial.printf` はワイヤフォーマットをフレーム途中で破壊する)。
- `dc-firmware/test/` ディレクトリは存在するが現在空。テストランナーは未セットアップ。

## 未対応項目

`TODO.md` が一次ソース (Launch Control の継続項目、proto まわり、ピン TODO)。`launch/`, `serial/`, `constants.hpp` あたりで非自明な作業を始める前に読むこと。
