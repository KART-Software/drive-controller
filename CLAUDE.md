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

ファームウェアは Arduino/Teensy ツールチェインを使っており clangd が混乱する。`.clangd` + `dc-firmware/.clangd_shim.h` で `-mcpu/-mfpu/-mthumb` 等の非互換フラグを剥がし、`WProgram.h` との `random`/`srandom` 衝突を回避するシムを pre-include している。ツールチェイン変更後に clangd が幻のエラーを出すようなら `compile_commands.json` を再生成 (`pio run` で自動的に走る)。

## アーキテクチャ

### ファームウェア並行性 (Teensy 4.1)

`dc-firmware/src/main.cpp` は 3 つの並行コンテキストで動く。共有状態を触る前に自分のコードがどれに属するか把握すること:

1. **モーター制御 ISR** — `IntervalTimer`, 周期 1 ms, NVIC 優先度 **0 (最高)**。センサー状態を読み、PID を回し、PWM を書き出す。`motor_controller.cycle()`。
2. **センサーサンプリング ISR** — `IntervalTimer`, 周期 125 µs (8 kHz)。既定 (`-DADC_DMA`, platformio.ini) では ADS8688 を **DMA (非ブロッキング SPI)** で駆動し、ISR は前回 DMA 結果を移動平均に反映して次の転送を kick するだけ (`sensor_hub.sampleAdcDmaIsr()`)。NVIC 優先度は **208** (USB より下) にして USB 送受信を阻害しないようにする。`-DADC_DMA` を外すとブロッキング読み (`sensor_hub.read()`) を `loop()` で行うフォールバックになる。
3. **`loop()`** — 非 ISR: IMU 読み (DMA 経路では ADC と分離)、CAN poll/send (60 Hz)、パルスカウンタ更新 (車輪速 + RPM)、プラウシビリティチェック、シリアルプロトコル、コマンドディスパッチ、オートシフター tick (毎イテレーション)、launch FSM tick (凍結時はビルドから除外)。テレメトリ送信は TX バッファ満杯時にドロップする (loop をブロックさせない, `serial_protocol.cpp`)。

ISR と loop の間で共有される可変状態は **必ず保護する**。`MovingAverage<N>` は既に `sum` の読み取りを `noInterrupts()` で守っている — このパターンを踏襲すること。センサーの `update()` を呼ぶのは ISR のみで、読み手は移動平均経由で十分整合した値を見る。

### センサー / ターゲットの所有

- `SensorHub` がすべてのセンサーと `EtcTarget` を所有する。多くのコードは `SensorHub&` を `const` で受け取り、`Configurator` だけが `SensorHub::mut` 経由で mutable アクセサに到達する (意図的な friend-by-API)。
- `EtcTarget` がターゲットスロットル開度を決める: APPS と ITTR (IST コントローラからの CAN コマンド) の切り替え、モード (Normal / Restricted / Calibration / MotorOff)、アイドリングおよびモード別キャップ、オプションの多項式ターゲットカーブ。

### パルスカウント (車輪速 / RPM)

回転系は **2 系統のハードウェア機構**で計測する (`SensorHub` が所有し、`loop()` から `updatePulse()` を `PULSE_UPDATE_INTERVAL_MS` 毎に呼ぶ):

- **車輪速 FL/FR/RL/RR** — `WheelSpeedSensor` (`sensor/wheel_speed.{hpp,cpp}`)。**FreqMeasureMulti** (FlexPWM 入力キャプチャ = 周期計測) を使う。各輪は **別々の FlexPWM サブモジュール**のピンに割り当てること (同一サブモジュールの 2 ピンは同時計測不可)。一定時間 (`STALE_MS`) エッジが無ければ 0Hz に減衰 (停車判定)。
- **エンジン RPM / クラッチ後 (出力軸) RPM** — `PulseCounter` (`sensor/pulse_counter.{hpp,cpp}`)。Teensy の **QuadTimer** 外部エッジカウンタ。QuadTimer 対応ピン (10–15,18,19) のみ。`CNTR` 差分 ÷ 経過時間で Hz を算出。

両者は `getFrequencyHz()` / `getRpm()` / `count()` の同一インターフェースを持つので、`AutoShifter` / `LaunchController` は backend を意識せず const 参照で受け取る。FlexPWM ピンは QuadTimer で数えられず、QuadTimer ピンは ADC(SPI0=10–13) を除くと残り少ないため、この 2 系統併用になっている。現在のピン割り当ては `constants.hpp` の Pulse Counter Settings を参照。

### CAN

- `CanController` + `CanBus` (FlexCAN_T4 サブモジュール, **CAN3 = Teensy 4.1 の pin 30/31**。CAN1 の 22/23 はモーター PWM/DIR に割当済み) — TX は ~60 Hz でジャイロ・加速度・ギアフレーム (`0x600-0x603`) を送信。RX (poll 駆動): `MODE_SELECT (0x200)` で ETC モード選択、`LAUNCH_CTRL (0x300)` で launch を切り替え、`AUTO_SHIFT (0x400)` でオートシフター ON/OFF を切り替え (byte0=0x01 で ON)。
- `CanRxData::checkTimeouts()` が安全層。フレーム受信時刻を `lastXFrameMs` に刻み、途絶時はモードを NORMAL に、launch を false に、autoShift を false (OFF=手動) にフォールバック。**`MOTOR_OFF` はラッチ状態であり、CAN 断で自動復帰させない。** 自動復帰パスを追加しないこと。
- `MODE_SELECT` で `UNSPECIFIED (0)` や未知値を受信した場合は **現在モードを維持** し、`lastModeFrameMs` のみ更新する (ハートビート扱い、意図的設計)。

### オートシフター

`shift::AutoShifter` (`dc-firmware/src/shift/`) — クイックシフター/シーケンシャル
ミッションの UP/DOWN シフト信号を制御する。設計仕様は `dc-firmware/auto_shifter_spec.md` が一次ソース。

- ON/OFF は CAN `AUTO_SHIFT (0x400)` で切替。OFF=manual (ドライバー判断)、ON=auto (独自ロジック)。
- 出力は **両モードとも「エッジ検出 → 整形パルス」** (`Idle → Pulsing → Cooldown`)。OFF はレベルミラーではない。
- パルス幅は transmission/ギア/方向/停車状態で決まる (config 化): IST=単一幅、NORMAL=走行中100ms (N スキップ)・停車中1速UP/2速DOWN 25ms (N 入れ)。
- ON 走行ロジックは RPM + 車輪速 + ギア + **APPS スロットルゲート** (アクセルオンで UP / オフで DOWN → ハンチング根治)。ダウンは速度ゲートなし。
- 4 層構成: 判断ロジック (`evaluate`, 将来の拡張点) / 調停 / 出力整形 / I/O。
- `SensorHub` 全体ではなく必要センサー (engine/wheelFL/wheelFR/gps/apps1) だけを const 参照で受け取る。`update()` は `loop()` から毎イテレーション呼ばれ、`autoOn = CAN active && plausibilityOK`。
- シフト機構制御 (クラッチ/点火カット/ブリッピング/オーバーレブ保護) は IST コントローラ責務。

### Launch Control (現在凍結)

**`-DLAUNCH_CONTROL_ENABLED` が未定義の通常ビルドでは凍結中。** `main.cpp` の `launchController.begin()` と FSM tick が `#if defined(LAUNCH_CONTROL_ENABLED)` でガードされ、`update()` が呼ばれないため FSM は Idle のまま (clutch motor 停止)。インスタンスと参照はビルドに残るので、フラグ ON ビルド (CI 等) で腐敗検知できる。CAN `LAUNCH_CTRL` は受信し続けるが何も起きない。

`launch::LaunchController` は FSM `Idle → Ready → Approach → EngageControl → FullEngage → Idle`。制御量は **エンゲージ率** (`wheel_rps_at_engine / engine_rps`) で、外側 PID がクラッチ位置指令を出す。内側 PID は `ClutchMotor` 内でクラッチセンサに対するクローズドループを構成する想定 — proto/config 上は配線済みだがアクチュエータ側はハード待ち (TODO.md A-2 参照)。

`BitePointEstimator` は Approach 中にクラッチミートポイントを学習する (走行毎に EMA)。`BitePointFile` は `Config` とは別に flash に保存される (`/bite_point.pb`)。有効時の `update()` は `LAUNCH_UPDATE_INTERVAL_MS` (50ms, 20Hz) の独立タイミングで呼ばれる (CAN TX 周期から分離済み)。

### 設定 / Flash 永続化

`Configurator` が LittleFS バックエンドの flash (`Flash` → `LittleFS_Program`, ファイル `/config.pb`) への書き込みを単独で所有する。

重要な不変条件: 部分 `dc_Config` が来たとき (例: `SetConfigCmd` で 1 セクションだけ編集) のマージは **必ず** `overlayConfig()` を経由すること。これはトップレベルサブメッセージごとに `has_*` を確認し、ゼロ値サブメッセージをスキップする。`config = src` で直接代入すると無関係のキャリブレーション値を黙ってゼロで上書きしてしまう。`dc_Config` にトップレベルサブメッセージを追加した場合は `overlayConfig()` に 1 行追加する (該当箇所にコメントあり)。

`calibrate()` は一方向の適用ステップ (config → 実センサー / コントローラ)。構造変更後はユーザに通知する前に必ず呼ぶこと。

### コマンドルーティング

ホストからのコマンドは protobuf `Command` の `oneof body`。`CommandRouter` が oneof タグ (`which_body`) でハンドラを振り分け、ハンドラは `CommandController::registerCommands()` で登録する。新コマンド追加手順: proto を拡張 → 両側で再生成 → `CommandController` にハンドラを追加 → `router.on(dc_Command_<field>_tag, ...)` で登録。

### プラウシビリティ / エラーハンドリング

`PlausibilityValidator` は `loop()` 毎にチェックされる。ETC のアーミング (モーター ON/OFF) は loop の **SHUTDOWN 安全層**が一元管理する。ETC を止める要因は独立に 3 つ:

- **① プラウシビリティ違反** → ETC 停止 + `SHUTDOWN_RELAY_PIN`=LOW (点火系 SHUTDOWN 回路を遮断、**復帰不可ラッチ**)。
- **② `SHUTDOWN_SIG_IN_PIN`=LOW** (外部 AND 回路が開いた) → ETC 停止 (RELAY は HIGH 維持、SIG が HIGH に戻れば**自動再開**)。
- **③ CAN `MOTOR_OFF` モード** → ETC 停止 (RELAY 維持)。

ETC 停止 = モーター OFF + モーター ISR 停止 (`motorControlTimer.end()`)、再開 = `setMotorOn()` (内部で `pid.reset()`) + ISR 再起動。① は復帰不可で電源再投入が必要。詳細ロジックは `main.cpp` の「ETC アーミング」コメントが一次ソース。**`SHUTDOWN_RELAY_PIN`(点火系) を落とすのは①だけ**で、`DcMotor` が持つモーター電源リレー `DC_MOTOR_RELAY_PIN` とは別系統。(燃料ポンプ制御は削除済み — 燃料カットはエンジン ECU 責務。)

### ピンアサイン

`dc-firmware/src/constants.hpp` で `#define` によりモータードライバを選択 (現在は `G2_18V17`)。主要ピンは実配線に合わせて確定済み: モーター(G2)=SLP21/PWM22/DIR23/FLT20、車輪速=24/25/28/36 (FlexPWM, 別サブモジュール)、RPM=Engine14/クラッチ後15 (QuadTimer)、オートシフター=UP_IN40/DOWN_IN39/UP_OUT4/DOWN_OUT5、CAN=CAN3(30/31)、ADC(SPI0)=10-13、IMU(SPI1)=0/1/26/27。パルスカウントのピンはペリフェラル制約あり (上記「パルスカウント」節)。ベンチテスト前に実配線と必ず照合すること。

## 規約

- C++ namespace はサブシステム単位: `etc::` (スロットル), `launch::` (Launch Control), `shift::` (オートシフター)。センサーと共通ユーティリティはグローバル。
- コメントは日本語の箇所が多い。新規コードは英語でも可だが、修正時は周囲のファイルに合わせる。
- 実行時テレメトリを `Serial` に直接出力しない。`SerialProtocol::sendDebugf` 経由で出す (COBS フレーミングを通る)。素の `Serial.printf` はワイヤフォーマットをフレーム途中で破壊する。
- `dc-firmware/test/` ディレクトリは存在するが現在空。テストランナーは未セットアップ。

## 未対応項目

`TODO.md` が一次ソース (Launch Control の継続項目、proto まわり、ピン TODO)。`launch/`, `serial/`, `constants.hpp` あたりで非自明な作業を始める前に読むこと。
