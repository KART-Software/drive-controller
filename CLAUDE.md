# CLAUDE.md

このファイルは Claude Code (claude.ai/code) がこのリポジトリで作業する際のガイドです。

## リポジトリ構成

2 つの連携サブプロジェクトが `spec/proto/` の Protobuf スキーマを共有しています:

- `dc-firmware/` — Teensy 4.1 ファームウェア (PlatformIO + Arduino framework)。ETC (電子スロットル)、オートシフター、Launch Control (現在凍結)、センサーサンプリング、CAN、シリアルプロトコルを担当。
- `dc-boot/` — 起動の選択だけをする薄いファーム (flash の先頭 0x60000000、ROM が飛んでくる唯一のイメージ)。`SRC_GPR10` のフラグで `dc-firmware` か `dc-sd-reader` へジャンプ。`dc-firmware` のビルド時に自動で合成される。
- `dc-sd-reader/` — SD リーダーファーム (別イメージ、flash の 0x60080000)。SD カードを USB の MTP で PC に見せるだけ。3 つの配置とフラグの約束は `dc-firmware/src/util/boot_layout.hpp` (`docs/loop_nonblocking_spec.md` §7.11)。予定: `dc-firmware` → `dc-main` に改名し、`dc-firmware/` の下に 3 つを入れる。
- `dc-console/` — Preact + Vite の Web アプリ。Web Serial API 経由でファームウェアと USB シリアル通信し、ライブモニタリング・キャリブレーションを行う。
- `spec/proto/drive_controller.proto` — ホスト ↔ デバイス間ワイヤフォーマットおよび永続化 `Config` の単一ソース。

proto のワイヤフォーマットは `COBS( protobuf_bytes ‖ crc16_le ) 0x00` — `dc-firmware/src/serial/` および `dc-console/src/protocol.ts` を参照。

## ビルド / 開発コマンド

### ファームウェア (PlatformIO)

```bash
# ビルド
pio run -d dc-firmware -e teensy41

# Teensy にアップロード (firmware.hex には post_build.py が dc-boot を合成済み。dc-sd-reader は残る)
pio run -d dc-firmware -e teensy41 -t upload

# dc-sd-reader も書く / 初めて書く: dc-boot + dc-firmware + dc-sd-reader を 1 つの hex にして書く
# (dc-sd-reader の hex だけを書くと先頭の dc-boot が消えて起動しなくなる)。Windows / macOS でも可
python3 tools/flash_all.py

# ヘッドレス Linux (GUI の Teensy Loader が無い / 動かない): 同梱の teensy_loader_cli で書く。初回は udev ルールを入れる
#   sudo cp ~/.platformio/packages/tool-teensy/00-teensy.rules /etc/udev/rules.d/ && sudo udevadm control --reload
#   console (ブラウザ) が COM を掴んでいると 134 baud を当てられないので Disconnect しておく
uv run tools/flash_all.py                      # 3 つまとめて (134 baud でブートローダに入れて -w、3 回まで再試行)
stty -F /dev/serial/by-id/usb-Teensyduino_KART_Drive_Controller_*-if00 134; sleep 3   # dc-firmware だけ手で書くとき
~/.platformio/packages/tool-teensy/teensy_loader_cli -mmcu=TEENSY41 -w -v dc-firmware/.pio/build/teensy41/firmware.hex

# 端末からのモニタ / コマンド送信 (console の代わり。Linux / Windows / macOS、ポートは自動)
uv run tools/dcmon.py -t 5 --can --control     # debug 行 + 1 s ごとの sys / can / control
uv run tools/dcmon.py --send reboot mode=1     # コマンド (例: SD リーダーへ)

# シリアルモニター (115200 baud)
pio device monitor -d dc-firmware
```

初回 clone 後は `git submodule update --init --recursive` で `lib/kart-can` (CAN 定義) を取得すること。

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

1. **`tickISR`** — GPT2, 周期 1 ms, NVIC 優先度 **64**。毎回、SIG_IN 読み → `ControlInput::update()` → モード反映 → プラウシビリティとアーミング (`armEtc()`) → **モーター制御** (`motorController.cycle()`、ETC が ON のときだけ) → 同定実験 → オートシフター (`tick()`) → 回転計測 (毎 ms、エンジン / クラッチ後は直近 100 ms の窓) → CAN 送信 (0x600-0x603 は 16 ms、0x60A は 33 ms) → **SD ログ 1 レコードの採取** (`buildLogRecord()` → リング) の順。ETC の開始 / 停止 (`startEtc()` / `stopEtc()`) はここからのみ。所要は `LoopStats` の `tick_max_us` (約 20 µs)。
2. **`canRxISR`** — FlexCAN3 の受信 (FIFO) 割り込み, 優先度 **64**。`CanRxData` に取り込み、0x741 のシフト入力はその場で `autoShifter.onPaddles()` に渡す。tick と同じ優先度なので互いに割り込まず、シフター・`CanRxData`・`ControlInput` はこの 2 つの間で排他不要。FlexCAN の `events()` は呼ばない (呼ぶとコールバックがキュー経由になる)。
3. **センサーサンプリング ISR** — `IntervalTimer` (PIT のチャネル 2。0 / 1 は時刻の Lifetime Timer), 周期 62.5 µs, 優先度 **208** (USB より下)。ADS8688 を **DMA (非ブロッキング SPI)** で駆動し、ISR は前回 DMA 結果を移動平均に反映して次の転送を kick するだけ (`sensor_hub.sampleDmaIsr()`)。IMU (IAM-20680HP、SPI1 = LPSPI3) も同じ ISR の `IMU_SAMPLING_DIVIDER` (= 2) 回に 1 回 (8 kHz) で DMA (CS はハードウェア PCS0 = pin 0、120 bit の 1 フレーム)、移動平均 8 サンプル (1 ms)。**SPI1 は IMU 専用** (トランザクションを開いたまま CS をハードウェアにするので、ほかのデバイスはつなげない)。値は `Imu::accel()` / `gyro()` (`interfaces/sensor/imu.hpp`) で移動平均から読む。ブロッキングで読むのは setup の初期化だけ (DMA なしの経路は廃止した)。
4. **`loop()`** — 非 ISR: telemetry (50 Hz)、コマンドディスパッチ、SD ログの書き出し (リングからまとめて。カード挿入時)、ISR が積んだ debug の送信、launch FSM tick (凍結時はビルドから除外)。シリアル送信は全フレームが TX リングの空きを確認し、足りなければ捨てる (loop をブロックさせない, `serial_protocol.cpp`)。50 Hz telemetry は TX バッファ 1 本分 (2 KB) を残して先に捨てるので、コマンド応答と debug は最後まで届く。

**時刻は `Clock` で、`millis()` / `micros()` は使わない。** `Clock` はインターフェース (`interfaces/util/clock.hpp`)、実装は `SystemClock` (`util/system_clock.hpp`)。時刻源は PIT の Lifetime Timer (64 bit、24 MHz。ソフトリセットでも 0 に戻る。DWT はソフトリセットで戻らないので使わない)。`SystemClock` は状態を持たず、コンストラクタで PIT を動かす (その時点が時刻の 0。static 初期化なので `IntervalTimer` より前)。`Clock` は読んだ瞬間の時刻 (`nowUs64()` / `nowUs()` / `nowMs()`) だけを返す。tick は先頭で 1 回読んで部品に配る。実体 `sysClock` は `main.cpp` だけが持ち (extern しない)、部品へは時刻を引数で渡すか `Clock` の参照を渡す (`DebugLogger::initialize()`、`StopWatch` を使う部品のコンストラクタ)。区間の計測やタイムアウトは `StopWatch` (`util/stop_watch.hpp`)。時刻はクラスのメンバーにしまわず、使う関数まで引数で渡す (`nowMs_` を持つと、別の文脈が刻んだ時刻のほうが新しくなって差が負になる)。別の文脈 (CAN 受信割り込みなど) が刻んだ時刻との差は符号付きで扱い、新しいものは経過 0 とみなす。`src/interfaces/` はインターフェースを置く場所で、中は `src/` と同じ構成にする。

**ISR (tick / CAN 受信) から `Serial` / SD / flash / `malloc` / `delay` を呼ばないこと。** debug はどこからでも `DebugLogger::log()`。`DebugLogger` は時刻を付けて出力先 (`LogWriter`、`interfaces/util/log/`) に配るだけで、その場で出せるかは出力先が判断する。USB へ送る `SerialDebugWriter` は ISR の中や割り込み禁止中、USB がつながっていない間 (起動時に接続は待たない) はキューに積み、loop の `DebugLogger::service()` (→ 各 writer の `service()`) が送る。ISR から呼ぶときの書式は整数と文字列だけ (newlib の浮動小数点書式は malloc する)。loop から tick の状態を変えるときは、要求を置いて tick に処理させる (例: `ExperimentRunner::requestStart()`) か、短い `noInterrupts()` 区間で書く (例: `ControlInput::setOverride()`)。

**flash (LittleFS) への書き込み中は割り込みが約 150 ms 止まる。** `Flash` が `FlashWriteGate` で tick に知らせ、tick は ETC を止めてから (シフトのパルス中は待つ) 書き込みを許可し、終われば再開する (§6.8)。書き込みは必ず `Flash` 経由で行うこと。

ISR と loop の間で共有される可変状態は **必ず保護する**。`MovingAverage<N>` は合計を持ち越さず `getAvg()` のたびに N 個を足し直す (float の丸め誤差が積み重ならない)。`add()` は 32 bit の要素を 1 個書くだけなので、どの文脈から読んでも壊れた値は見えず、割り込みを止める必要はない。センサーの `update()` を呼ぶのは ISR のみで、読み手は移動平均経由で十分整合した値を見る。

### センサー / ターゲットの所有

- `SensorHub` がすべてのセンサーと `EtcTarget` を所有する。**見るだけの部品には `SensorView` (`sensor/sensor_view.hpp`、`sensorHub.view()`) を渡す**: 各センサーへの const 参照の束で、書き換えるメソッドを持たない (型で保証。const の付け忘れでは抜けられない)。`SensorHub&` (書き換え可の getter: 校正の適用、モード反映、回転センサーの設定) を持つのは `main.cpp` と `Configurator` だけ。`EtcTarget` を書き換える部品 (`ExperimentRunner`、`CommandController`) には `sensorHub.target()` を `EtcTarget&` として明示的に渡す。旧 `SensorHub::Mut` (`mut`) は 2026-10-21 に廃止。
- `EtcTarget` がターゲットスロットル開度を決める: APPS と ITTR (IST コントローラからの CAN コマンド) の切り替え、モード (Normal / Restricted / Calibration / MotorOff)、アイドリングおよびモード別キャップ、オプションの多項式ターゲットカーブ。

### 回転計測 (車輪速 / エンジン / クラッチ後)

6 つの回転センサーを同じ形で扱う (`docs/loop_nonblocking_spec.md` §6.7)。`SensorHub` が所有し、`tickISR` が `updatePulse(nowUs)` を毎 ms 呼ぶ。

- **使う側の口**: `RotationSensor` (`interfaces/sensor/rotation_sensor.hpp`)。`sample()` で `RotationSample { hz, rpm, timeUs (窓の中心), windowUs, edges }`、`hz()` / `rpm()` / `count()` / `pulses()`。車輪は `WheelSpeedSensor` (`RotationSensor` + `mps()` / `kmh()`。タイヤ直径はセンサーが持ち、使う側は換算しない)。使う側 (`AutoShifter`・`StarterController`・`LaunchController`・telemetry・ログ) はハードウェアを知らず、歯数の換算もしない (rpm はセンサーが出す)。loop からも組の揃った値が読める (seqlock)。**tickISR より優先度の高い割り込みから `sample()` / `hz()` / `rpm()` を呼ばないこと** (tick の書き込み途中に割り込むと読み直しが終わらない)。
- **実体**: `EdgeRotationSensor` (エンジン / クラッチ後)、`EdgeWheelSpeedSensor` (車輪。中に `EdgeRotationSensor` を持つ合成 + タイヤ直径 `setDiameterMm()`)。
- **計算**: `EdgeRateEstimator` (ハードウェア非依存)。(エッジ番号, 時刻) の履歴から、窓 = 「N 歯か最大時間 T の短いほう」で回転を出す。両端は同じ向きのエッジに揃える (両エッジ時のデューティ比のずれを打ち消す)。エッジが来ない間は「1 歯 ÷ 経過時間」を上限に下げ、`stale_ms` で 0。
- **ハードウェア** (`EdgeSource`、`interfaces/sensor/edge_source.hpp`):
  - **エンジン (pin 14) / クラッチ後 (pin 15)** — `QuadTimerEdgeSource`。QuadTimer3 ch2 / ch3 でエッジを数え、空きの ch0 / ch1 を IP バス ÷64 で自走させて副入力でエッジ時刻をキャプチャ (割り込みなし)。入力選択 (daisy) を pin 14 / 15 のパッドに設定すること (リセット値は別パッド)。ピン 18 / 19 は `analogWrite` に使えない。
  - **車輪 FL/FR/RL/RR** — `FreqMeasureEdgeSource`。**FreqMeasureMulti** (FlexPWM 入力キャプチャ) の周期の列からエッジ時刻を組み立てる。各輪は **別々の FlexPWM サブモジュール**のピンに割り当てること (同一サブモジュールの 2 ピンは同時計測不可)。
- **config**: `Config.rotation` (`RotationConfig`): エンジン・クラッチ後・前輪・後輪 (左右共通) ごとに 歯数・窓の歯数・最大時間・止まり判定・両エッジ、車輪はタイヤ直径 (`setDiameterMm()`)。歯数は以前の `SensorCalib` から移した。設定は `setParams()` で置き、setup では `SensorHub::begin()` (config の適用の後に呼ぶ) が、実行中は次の tick が取り込む。
- telemetry と SD ログはパルスの Hz (`hz()`)。`count()` は両エッジならエッジ数 (パルス数の 2 倍)。

### CAN

- `CanController` (内容) + `CanBus` (interface、`interfaces/can/can_bus.hpp`) + `Flexcan3Bus` (自前の FlexCAN3 ドライバ、`can/flexcan3_bus.{hpp,cpp}`。仕様は `docs/can_bus_spec.md`)。**CAN3 = Teensy 4.1 の pin 30/31** (`CAN3_RX_PIN` / `CAN3_TX_PIN`。CAN1 の 22/23 はモーター PWM/DIR に割当済み)。フレームは `CanFrame { id, len, data[8] }` (kart-can の pack / unpack に合わせた素の構造体)。FlexCAN_T4 は 2026-10-20 に廃止 (ヘッダ内 static の翻訳単位問題で受信割り込みが無限に再入し loop・USB が止まった。仕様書 §1)。CAN 定義の単一ソースは submodule `lib/kart-can` (`can.yaml` → 生成 `kart_can.h`、仕様は同リポジトリの `docs/can-spec.md`)。TX (`tickISR` から): IMU + ギア (`0x600-0x603`, 16 ms)、センサー (`0x604` ペダル・クラッチ・ブレーキ圧 / `0x605` APPS・TPS 生値 / `0x607` 車輪速 km/h / `0x608` 車輪パルス累計 / `0x609` エンジン・クラッチ後 rpm、16 ms、0x600 系と半周期ずらす。`0x606` 後ろサスは ADC 未接続で送らない)、状態 `DC_Status (0x60A)` (33 ms: 適用中の ETC モード・オートシフト・Shutdown 回路)。車輪速の km/h は車輪センサー (`WheelSpeedSensor::kmh()`) が出す。**送信周期は kart-can が生成する `KART_CAN_*_CYCLE_TIME_MS` を使い、ファーム側で定数を持たない** (変えるときは kart-can の `can.yaml` を先に変える)。RX は受信割り込み (`canRxISR`)。RX: `Control (0x740)` (data_logger、33 ms: ETC モード / launch / auto-shift / starter) と `Shift (0x741)` (10 ms: シフトパドル上下)。
- **ドライバの要点**: 受信は ID ごとの受信メールボックス + 個別マスク (0x740 / 0x741 だけ。他のフレームでは割り込まない。Rx FIFO は CAN FD で使えないので使わない)。受信割り込み (優先度 64、tick と同じ) はメールボックスを読んでコールバック `canRxISR` を**割り込みの文脈**で呼び、末尾で ESR1 のフラグを消す。送信はメールボックス 8〜15 の空きに書き、空きがなければ捨てる (キュー・送信完了割り込みなし)。エラー割り込みは使わず、状態は `CanBus::state()` (active / passive / bus-off、TEC / REC)、回数は `stats()` で読む。バスオフは自動復帰 (ベンチ確認済み: 短絡中は約 2 ms 周期で復帰 → 再 bus-off を繰り返し、直せば 1 s 以内に active)。`begin()` は `CanConfig` (ビットレート、FD) を受け、対応していない設定には false を返す。CAN FD は構造だけ備えてある (`CanFrame` 64 B、メールボックス受信、ストライド定数。有効化は未対応、仕様書 §4.8)。状態は telemetry の `Sensor.can` (`CanStatus`: fault / TEC / REC / 受信・取りこぼし・捨ての累計) で console に出る (Control Input パネルと sys-stats 行)。fault が active で TEC が 0 ならバスは生きている (0x740 が来ないのは送り手の問題)。
- 途絶時のフォールバックは `ControlInput::update()` が持つ (`CanRxData` は受信した値と時刻だけを持つ)。Control が 200 ms 途絶したら mode を NORMAL に、launch / auto-shift / starter を false にフォールバック。**`MOTOR_OFF` はラッチ状態であり、CAN 断で自動復帰させない。** 自動復帰パスを追加しないこと。Shift は 100 ms 途絶で両パドル 0。起動後に一度も受信していない間は途絶扱いにしない。
- 制御フレームの byte0 で `UNSPECIFIED (0)` や未知値を受信した場合は **現在モードを維持** し、`lastControlFrameMs` のみ更新する (ハートビート扱い、意図的設計)。
- **制御入力は `ControlInput` (`src/control/`) が唯一の入口** (docs/control_input_spec.md)。消費側 (main のモード反映・アーミング③、`AutoShifter`、`ExperimentRunner`、ログ、telemetry) は入力元を知らない。ETC モードは (1) console の上書き → (2) 0x740 を起動後に一度も受信していなければ **MOTOR_OFF で待つ** → (3) CAN の受信値、の順で決まる。console の上書き (`SetControlOverrideCmd`) はベンチ用で RAM のみ。**0x740 を受信している間は拒否し、受信が始まったら自動解除**する (車載時にドライバーのスイッチを上書きしないため)。GPIO のモードノブ / auto-shift スイッチ / シフトパドル入力は廃止した。

### オートシフター

`shift::AutoShifter` (`dc-firmware/src/shift/`) — クイックシフター/シーケンシャル
ミッションの UP/DOWN シフト信号を制御する。設計仕様は `dc-firmware/auto_shifter_spec.md` が一次ソース。

- ON/OFF とシフトパドルは `ControlInput` から `tick({.autoOn, .upPressed, .downPressed}, nowUs)` で渡される (入手元は CAN 0x740 byte2 / 0x741、または console の上書き)。0x741 は受信割り込みから `onPaddles()` で**即時**に渡され、その場でパルスを始める (開始時刻は受信した瞬間の `sysClock.nowUs()`、チャタリング除去は送信側の責務)。OFF=manual (ドライバー判断)、ON=auto (独自ロジック)。パドルは押下状態で渡され、立ち上がり検出は `AutoShifter` が行う。
- 出力は **両モードとも「エッジ検出 → 整形パルス」** (`Idle → Pulsing → Cooldown`)。OFF はレベルミラーではない。パルスの開始から次の開始までは最短 0.5 s (`SHIFT_MIN_INTERVAL_MS`、config `cooldown_ms` が長ければそちら)。その間の要求は捨てる。
- パルス幅は transmission/ギア/方向/停車状態で決まる (config 化): IST=単一幅、NORMAL=走行中100ms (N スキップ)・停車中1速UP/2速DOWN 25ms (N 入れ)。
- ON 走行ロジックは RPM + 車輪速 (km/h、`min_wheel_kmh`。タイヤ直径が未設定だと常に停車扱い) + ギア + **APPS スロットルゲート** (アクセルオンで UP / オフで DOWN → ハンチング根治)。ダウンは速度ゲートなし。
- 4 層構成: 判断ロジック (`evaluate`, 将来の拡張点) / 調停 / 出力整形 / I/O。
- `SensorHub` 全体ではなく必要センサー (engine/wheelFL/wheelFR/gps/apps1) だけを const 参照で受け取る (車輪は `const WheelSpeedSensor&`。エンジンと車輪を取り違えると型で弾かれる)。`tick()` は `tickISR` から毎 ms 呼ばれ、オートシフトの ON/OFF だけで判定する (ETC プラウシビリティには非依存。auto-shift の安全フォールバックは CAN 断→manual)。
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

`SensorLogger` (`dc-firmware/src/util/log/`) が Teensy 4.1 内蔵 SD (SDIO、FIFO モード) へセンサーデータを **1 kHz・固定長バイナリ**で記録する (`docs/loop_nonblocking_spec.md` §7 が一次ソース)。**Config の flash (LittleFS) とは別系統**。

- 構成: `LogRing` (tick → loop の SPSC リング、2048 レコード ≈ 2 s、本体は `DMAMEM`) / `LogStore` (interface、`interfaces/util/log/`: 媒体とファイルの操作) ← `SdLogStore` (SdFat `SdFs` / `FsFile`) / `SensorLogger` (facade)。テキスト用の `LogWriter` ← `SerialDebugWriter` / `DebugLogger` と対になる。フォーマット (`LogHeader` 32 B / `LogRecord` 152 B。末尾に CAN の fault / TEC / REC) は `util/log/log_record.hpp`。
- **採取は `tickISR` の最後** (`SensorLogger::capture()` に `buildLogRecord()` を呼ぶラムダを渡す → 有効なら組み立ててリングへ)。1 ms の等間隔で、loop が SD で止まっても欠けない。SD ログが無効 (カードなし / 失敗で停止 / フォーマット中) の間は組み立てずに数えるだけ。`log_drops` は「記録されなかったレコードの数」(無効な間 + リングの溢れ) で、telemetry の `SysStats` と LogRecord に載る。カードなしの運転は想定外なので、drops が増え続けて console が警告色になるのは意図どおり。
- **書き出しは `loop()` の `service()`**: 4 KB 以上たまったら最大 8 KB をまとめて書き、2 s ごとに `sync()`。loop が止まってもリングが吸収する (3 s 止まると約 1 s 分が drops)。失敗したらその起動では止める (通知しない)。
- **カード**: 起動時に挿さっているときだけ。**exFAT でなければ (マウントできないものも) 起動時にフォーマットする** (カードはログ専用)。起動ごとに `dc_NNNN[_YYYYMMDD_HHMMSS]/` を作り、中に `0000.bin`, `0001.bin`, … を **100 MB ずつ事前確保**して順に書く (≈ 11.3 分/ファイル)。満杯なら一番古いファイル (最小の `NNNN` の最小の番号。空になったディレクトリも) を消す。ルートで見るのは `dc_` + 4 桁のディレクトリだけ。
- 時刻は `t_us` (64 bit、`Clock`)。絶対時刻はヘッダの `rtc_epoch` + `rtc_t_us` (起動時に 1 回) から。RTC は `setSyncProvider(Teensy3Clock.get)` で起動時反映、`set_rtc` で更新。
- `format_sd` (console の Format SD、確認ダイアログ): ETC 動作中 (`motorController.isOn()`) は拒否 (走行中に loop を止めないための歯止め)。ログを止めて exFAT にフォーマットし、新しいディレクトリで再開 (約 0.1 s)。
- レコード生成は**純粋関数** `buildLogRecord()` (`src/log_record_builder.{hpp,cpp}`) に集約 — 全入力を const 参照で受け、`SensorHub` 等の public const getter から読むだけで副作用なし。`SensorHub` はログ形式に依存しない。plausibility の valid/errors は `PlausibilityValidator::currentlyValid()` / `errorBits()` (副作用なし const) で読む。レコードの `loop_max_us` 等は loop が集計する値を tick が読む (診断用なので途中の値でもよい)。
- フォーマット変更時は `tools/decode_log.py` (ディレクトリ → CSV。ファイルをつなぎ、ゼロのレコードか `t_us` の飛びで止める) を合わせること。開発中は `SENSOR_LOG_VERSION` を 0 のまま上げない (固まったら上げる)。
- ホスト向けテレメトリの proto (`dc_Sensor`) とは別物: proto = 自己記述・可変長・50–60Hz、SD ログ = 高レート固定バイナリ (memcpy のみ)。
- **SD リーダーと起動の選択** (`docs/loop_nonblocking_spec.md` §7.11): 3 つのファーム = `dc-boot` (flash 先頭、起動の選択だけ) / `dc-firmware` (0x60010000、448 KB 以内にリンク) / `dc-sd-reader` (0x60080000)。console の「SD Reader」(`RebootCmd { mode: SD_READER }`、ETC 動作中は拒否) → `util/boot_mode` が `SRC_GPR10` の下位 28 bit にフラグ → ソフトリセット → `dc-boot` の `startup_early_hook()` (FLASHMEM、RAM 初期化前) がフラグを消して `dc-sd-reader` の IVT entry へジャンプ。`dc-sd-reader` は SD を USB の **MTP** ("KART SD Reader" / "SD Card"、PID 0x04D5) で見せ、点火リレー LOW。**USB を抜くか console の Reboot で `dc-firmware` に戻る** (console は PID で相手を見分け、SD リーダーのときは「通常モードで再起動」だけを出す。電源の入れ直しでも `dc-firmware`)。`dc-firmware` の USB は `USB_SERIAL` (PID 0x0483、製品名 "KART Drive Controller"、`src/usb_names.c`)。**`SRC_GPR10` の上位 4 bit は ROM の persist ビットなので触らない** (bit 30 で起動不能になる)。**eFuse (OCOTP) には触らない**。配置とフラグは `src/util/boot_layout.hpp` だけで決め、各プロジェクトの `.ld` と一致させる。`dc-sd-reader` を書くときは `python3 tools/flash_all.py`。`dc-sd-reader` の hex 単体を書かないこと。起動時の debug `boot: SRC_GPR10=… SRSR=…` がリセットの理由、直前がクラッシュなら `CrashReport` も debug に出る。
- ベンチ: `-DSENSOR_LOG_FILE_BYTES=1000000` でファイルを 1 MB にして切り替えを試せる。`teensy41_bench` の `debug_stall` (最大 5 s) で loop を止めて drops を確かめる。カードの中身は `local/sd-reader` (MTP ファーム) で取り出す。

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
