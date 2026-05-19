# CLAUDE.md

このファイルは Claude Code (claude.ai/code) がこのリポジトリで作業する際のガイドです。

## リポジトリ構成

2 つの連携サブプロジェクトが `spec/proto/` の Protobuf スキーマを共有しています:

- `dc-firmware/` — Teensy 4.1 ファームウェア (PlatformIO + Arduino framework)。ETC (電子スロットル)、Launch Control、センサーサンプリング、CAN、シリアルプロトコルを担当。
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
2. **センサーサンプリング ISR** — `IntervalTimer`, 周期 125 µs (8 kHz), NVIC 優先度 **16**。ADS8688 ADC を SPI で読み、移動平均を更新する。`sensor_hub.read()`。
3. **`loop()`** — 非 ISR: CAN poll/send (60 Hz)、パルスカウンタ集計、プラウシビリティチェック、シリアルプロトコル、コマンドディスパッチ、launch FSM tick。

ISR と loop の間で共有される可変状態は **必ず保護する**。`MovingAverage<N>` は既に `sum` の読み取りを `noInterrupts()` で守っている — このパターンを踏襲すること。センサーの `update()` を呼ぶのは ISR のみで、読み手は移動平均経由で十分整合した値を見る。

### センサー / ターゲットの所有

- `SensorHub` がすべてのセンサーと `EtcTarget` を所有する。多くのコードは `SensorHub&` を `const` で受け取り、`Configurator` だけが `SensorHub::mut` 経由で mutable アクセサに到達する (意図的な friend-by-API)。
- `EtcTarget` がターゲットスロットル開度を決める: APPS と ITTR (IST コントローラからの CAN コマンド) の切り替え、モード (Normal / Restricted / Calibration / MotorOff)、アイドリングおよびモード別キャップ、オプションの多項式ターゲットカーブ。

### CAN

- `CanController` + `CanBus` (FlexCAN_T4 サブモジュール) — TX は ~60 Hz でジャイロ・加速度・ギアフレーム (`0x600-0x603`) を送信。RX (poll 駆動): `MODE_SELECT (0x200)` で ETC モード選択、`LAUNCH_CTRL (0x300)` で launch を切り替え。
- `CanRxData::checkTimeouts()` が安全層。フレーム受信時刻を `lastXFrameMs` に刻み、途絶時はモードを NORMAL に、launch を false にフォールバック。**`MOTOR_OFF` はラッチ状態であり、CAN 断で自動復帰させない。** 自動復帰パスを追加しないこと。
- `MODE_SELECT` で `UNSPECIFIED (0)` や未知値を受信した場合は **現在モードを維持** し、`lastModeFrameMs` のみ更新する (ハートビート扱い、意図的設計)。

### Launch Control

`launch::LaunchController` は FSM `Idle → Ready → Approach → EngageControl → FullEngage → Idle`。制御量は **エンゲージ率** (`wheel_rps_at_engine / engine_rps`) で、外側 PID がクラッチ位置指令を出す。内側 PID は `ClutchMotor` 内でクラッチセンサに対するクローズドループを構成する想定 — proto/config 上は配線済みだがアクチュエータ側はハード待ち (TODO.md A-2 参照)。

`BitePointEstimator` は Approach 中にクラッチミートポイントを学習する (走行毎に EMA)。`BitePointFile` は `Config` とは別に flash に保存される (`/bite_point.pb`)。

呼び出しタイミング: `update()` は `loop()` から CAN TX 周期で呼ばれている。独立タイマー化の TODO あり (A-7)。

### 設定 / Flash 永続化

`Configurator` が LittleFS バックエンドの flash (`Flash` → `LittleFS_Program`, ファイル `/config.pb`) への書き込みを単独で所有する。

重要な不変条件: 部分 `dc_Config` が来たとき (例: `SetConfigCmd` で 1 セクションだけ編集) のマージは **必ず** `overlayConfig()` を経由すること。これはトップレベルサブメッセージごとに `has_*` を確認し、ゼロ値サブメッセージをスキップする。`config = src` で直接代入すると無関係のキャリブレーション値を黙ってゼロで上書きしてしまう。`dc_Config` にトップレベルサブメッセージを追加した場合は `overlayConfig()` に 1 行追加する (該当箇所にコメントあり)。

`calibrate()` は一方向の適用ステップ (config → 実センサー / コントローラ)。構造変更後はユーザに通知する前に必ず呼ぶこと。

### コマンドルーティング

ホストからのコマンドは protobuf `Command` の `oneof body`。`CommandRouter` が oneof タグ (`which_body`) でハンドラを振り分け、ハンドラは `CommandController::registerCommands()` で登録する。新コマンド追加手順: proto を拡張 → 両側で再生成 → `CommandController` にハンドラを追加 → `router.on(dc_Command_<field>_tag, ...)` で登録。

### プラウシビリティ / エラーハンドリング

`PlausibilityValidator` は `loop()` 毎にチェックされる。失敗時はモーターを OFF し、`FUEL_PUMP_PIN` 経由で燃料ポンプを切る。モーター OFF と同時にモーター ISR も停止 (`motorControlTimer.end()`)。再アーミングは自動では行われず、電源再投入か明示的キャリブレーションフローが必要。

### ピンアサイン

`dc-firmware/src/constants.hpp` で `#define` によりモータードライバを選択 (現在は `G2_18V17`)。多くの TODO 付きピン番号がプレースホルダのまま (TODO.md F セクション)。ベンチテスト前に実配線と必ず照合すること。

## 規約

- C++ namespace はサブシステム単位: `etc::` (スロットル), `launch::` (Launch Control)。センサーと共通ユーティリティはグローバル。
- コメントは日本語の箇所が多い。新規コードは英語でも可だが、修正時は周囲のファイルに合わせる。
- 実行時テレメトリを `Serial` に直接出力しない。`SerialProtocol::sendDebugf` 経由で出す (COBS フレーミングを通る)。素の `Serial.printf` はワイヤフォーマットをフレーム途中で破壊する。
- `dc-firmware/test/` ディレクトリは存在するが現在空。テストランナーは未セットアップ。

## 未対応項目

`TODO.md` が一次ソース (Launch Control の継続項目、proto まわり、ピン TODO)。`launch/`, `serial/`, `constants.hpp` あたりで非自明な作業を始める前に読むこと。
