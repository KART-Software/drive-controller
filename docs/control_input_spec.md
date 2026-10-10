# 制御入力の CAN 化 + プラウシビリティフラグのカプセル化 仕様書 (ドラフト)

Status: **実装済み (2026-10-07, PR #9 の上に積んだ PR)。**

実装時の変更: override の未指定項目は「上書き中なら今の上書き値を保つ」にした (オートシフトだけ切り替えるとモードの上書きが消え、ETC が止まっていたため)。AutoShifter には ControlInput を持たせず、`update({.autoOn, .upPressed, .downPressed})` で値を渡す (shift → control の依存を作らない)。
作成: 2026-10-07。関連: CLAUDE.md「CAN」節、[etc_experiment_mode_spec.md](etc_experiment_mode_spec.md) §5/§6。

## 1. 決定事項 (2026-10-07)

- **ETC モードとオートシフト ON/OFF は、どちらも CAN 経由で受ける。**
- `PlausibilityValidator` のチェックフラグは **private** にする。構造体にはまとめない。
- フラグの書き込み経路は **`setCheckFlags()` だけ** にする。
- Q1: kart-can は更新済み (307e038, 2026-10-05)。0x740 の送り手は `data_logger` (kmm M コア)。
- Q2: GPIO 経路は **完全に削除** (ビルドフラグで残さない)。
- Q3: 0x740 を起動後に一度も受信していない間は **MOTOR_OFF (ETC 停止) で待つ**。
- Q4: ベンチでは **console から制御入力を override** できるようにする (§3.3)。
- Q5: console に **CAN 制御入力の受信状態を表示** する。
- Q6: GPIO のノブ・スイッチのピン定義と読み取りコードは **削除**。

- Q7: スコープは推奨どおり。ただし Q10 (GPIO パドルを残さない) と両立させるため、**0x741 Shift の受信は本仕様に含める** (下記)。セル (starter) とセルモーターリレー、0x604–0x609 送信は別仕様。Vbat (0x5F1) は小さな別 PR で先行。
- Q8: override は「0x740 受信中は不可、受信開始で自動解除」。
- Q9: override 時の console 確認ダイアログは **不要**。
- Q10: GPIO のパドル入力 (`AUTO_SHIFT_UP/DOWN_IN_PIN`) は **残さない**。0x741 の受信で置き換える。
- Q11: kart-can サブモジュールを 307e038 に更新 (本 PR の先頭コミット)。
- Q12: MOTOR_OFF 待機の追加通知は **不要** (telemetry と 0x60A のみ)。

### 1.2 0x741 Shift をこの仕様に含める理由

Q7 で Shift を別仕様にし、Q10 で GPIO パドルを削除すると、その間は手動シフトの入力がどこからも来ない。そこで本仕様では **0x741 の受信だけ** を扱い、`AutoShifter` が読んでいたパドル入力の入手元を GPIO から `ControlInput` に差し替える。オートシフターの判断・出力整形・出力ピンには手を入れない。

- `ControlInput::shiftUpPressed()` / `shiftDownPressed()`: 0x741 の押下状態。`AutoShifter` は今どおり自分でエッジ検出する (kart-can の規則「0→1 の立ち上がりを 1 回の要求」と一致)。
- フェイルセーフ: 0x741 が 100 ms 途絶したら両パドル 0 扱い (kart-can 規則)。
- 0x741 も一度も受信していない間は 0 (押されていない) 扱い。

### 1.3 kart-can 307e038 で増えた、このノードに関わる定義

| ID | 名称 | 向き | 内容 |
|---|---|---|---|
| 0x740 | Control | data_logger → dc | DLC 4 (旧 3)、33 ms。byte0 etc_mode / byte1 launch / byte2 auto_shift (互換) / **byte3 starter (セル、押下中 1)** |
| 0x741 | Shift | data_logger → dc | DLC 2、10 ms。シフトパドル上下の押下状態。0→1 の立ち上がりを 1 回の要求とみなす |
| 0x60A | DC_Status | dc → | 適用中の制御状態・リレー状態・Shutdown ループ状態。0x740 送信の後継 |
| 0x604–0x609 | (センサー類) | dc → | APPS/TPS 物理値と生値、ストローク、車輪速、回転数 |
| 0x5F1 | MoTeC_Engine2 | motec → dc | `battery_voltage` を Vbat として使用 |

フェイルセーフ (kart-can docs/can-spec.md §4.1): Control 200 ms 途絶で etc_mode → NORMAL (MOTOR_OFF はラッチ)、launch → inactive、auto_shift → manual、starter → 0。Shift 100 ms 途絶で両パドル 0。
本仕様の Q3 (未受信の間は MOTOR_OFF) は「一度も受信していない」場合の規則で、途絶時の規則とは別。

## 2. 現状

| 項目 | 今の実装 |
|---|---|
| ETC モード | GPIO 3 ピンのノブ (`SensorHub::modeSwitch()`) → `selectToEtcMode()` (CAN モジュール内) で変換 |
| オートシフト ON/OFF | GPIO スイッチ (`SensorHub::autoShiftSwitch()`) |
| CAN 0x740 Control | **このノードが送信**している (GPIO の状態を流す, `CanController::send` → `CanBus::sendControl`)。kart-can も 1a01108 で送り手を drive_controller に変更済み |
| CAN 受信パース | `CanRxData::mergeFrame` / `checkTimeouts` に実装済みだが `#if defined(CONTROL_INPUT_VIA_CAN)` で無効 (未定義) |
| 途絶時フォールバック (受信コード内) | mode → NORMAL (**MOTOR_OFF はラッチ、CAN 断で自動復帰させない**)、auto-shift → OFF (手動)、launch → false。タイムアウト `CAN_CONTROL_TIMEOUT_MS` = 200 ms |
| 起動後 未受信時 | mode = NORMAL、auto-shift = OFF (既定値のまま) |

制御入力の消費側は 5 か所: `main.cpp` (モード反映とアーミング③)、`AutoShifter` への autoOn、`ExperimentRunner` (開始条件・ガードの MOTOR_OFF 判定)、`log_record_builder.cpp` (flags の auto-shift ビット)、`CanController::send` (0x740 送信)。

フラグの外部からの直接書き込みは `ExperimentRunner` だけ (退避・復元)。これは PR #8 で一時停止 API に置き換え済み。読み手は外部に無い。

## 3. 設計

### 3.1 ControlInput モジュール

新設 `src/control/control_input.{hpp,cpp}`。依存は「control → can (受信データ)」の一方向。

```cpp
class ControlInput {
   public:
    explicit ControlInput(const CanRxData& rx);
    void update(unsigned long nowMs);  // 新しいフレームの取り込み・途絶時のフォールバック
    EtcTarget::Mode etcMode() const;
    bool autoShiftOn() const;
    bool launchOn() const;        // 凍結中 (常に false で消費されない)
    bool linkAlive() const;       // 0x740 を CAN_CONTROL_TIMEOUT_MS 以内に受信しているか
};
```

- 消費側 5 か所は `ControlInput` だけを見る。CAN の型 (`CanEtcMode`、`CanRxData`) は CAN モジュールと `ControlInput` の外に出さない。
- **安全規則は `ControlInput` に集める。** `CanRxData` は最後に受信したフレームの値と受信時刻だけを持ち、途絶時のフォールバック
  (NORMAL、MOTOR_OFF はラッチ)・未受信時の MOTOR_OFF 待機・override はすべて `ControlInput` が判断する。
- ETC モードの型はドメインの `EtcTarget::Mode` に統一する。変換は「CAN ↔ ドメイン」を `can_data.cpp`、
  「proto ↔ ドメイン」を `serial_protocol.cpp` の 2 か所だけに置く。0x60A の `CanStatusData` もドメインの型で受け取る。
- `ExperimentRunner` は `SensorHub` の代わりに `ControlInput` を受け取って MOTOR_OFF を判定する (ETC → CAN の依存が消える)。
- `CONTROL_INPUT_VIA_CAN` マクロは廃止し、CAN 受信を唯一の経路にする (Q2)。
- `etcMode()` の決定順: (1) console override が有効ならその値 (§3.3) → (2) 0x740 を一度も受信していなければ `MotorOff` (Q3) → (3) 受信値 (UNSPECIFIED は現在値を維持、途絶時は NORMAL、ただし MOTOR_OFF は保持)。
- `source()`: `Can` / `Override` / `WaitingForCan` を返す。console 表示と 0x60A に使う。

### 3.2 CAN 送受信の変更

- `CanRxData` の 0x740 パースを常時有効にする。`checkTimeouts()` は廃止し、途絶判定は `ControlInput` へ移す。
- このノードからの 0x740 送信 (`CanController::send` の `sendControl`) を削除する。
- **kart-can の Control メッセージの送り手を別ノードに戻す必要がある** (1a01108 の逆)。どのノードか → Q1。

### 3.3 console からの override (ベンチ用, Q4)

- 新コマンド `SetControlOverrideCmd { bool enable; optional EtcMode etc_mode; optional bool auto_shift; }`。RAM のみ (flash に保存しない、再起動で解除)。
- 安全上の制約 (案, Q8): **0x740 を受信している間は override を受け付けない** (ok=false)。override 中に 0x740 を受信し始めたら override を自動解除し、debug で通知する。車載時にドライバーのスイッチを console が上書きする事故を防ぐため。
- telemetry に制御入力の状態を追加 (§3.4)。

### 3.4 受信状態の表示 (Q5)

- proto `State` に `ControlStatus control = 5` を追加: `source` (CAN / OVERRIDE / WAITING_FOR_CAN)、`link_alive`、`last_rx_age_ms`、受信した生の `etc_mode` / `auto_shift` / `starter`。
- console は ModeKnob 付近に「CAN 受信中 / 途絶 / 未受信 (MOTOR_OFF 待機) / override 中」を表示し、override の操作 UI を置く。

### 3.5 プラウシビリティフラグ

- `appsCheckFlag` 〜 `bpsTpsCheckFlag` の 9 個を private に移す。
- 書き込みは既存の `setCheckFlags(bool × 9)` のみ。呼び出し元は `Configurator` の 2 か所 (`calibrate()` 内の config 適用、`setPlausibilityFlags` コマンド) のまま。
- 外部の読み手が無いので getter は追加しない。
- 実験モードの一時停止は `suspendTargetCheck()` (PR #8) で、フラグには触れない。

## 4. 安全上の注意

- **MOTOR_OFF のラッチ規則が GPIO 時代と変わる。** GPIO では「ノブを戻せば ETC 再開」だった。CAN では途絶時に MOTOR_OFF を保持する (CLAUDE.md の既存規則)。明示的に別モードを受信すれば再開する。
- **起動後、0x740 を一度も受信していない間は MOTOR_OFF (ETC 停止) で待つ** (Q3)。送り手ノード (data_logger) が起動していなければ ETC は動かない。オートシフトは手動。
- 一度受信した後の途絶は kart-can の規則どおり (mode → NORMAL、ただし MOTOR_OFF は保持)。「未受信」と「途絶」は別の状態として扱う。

## 5. ベンチへの影響

- data_logger が無いベンチでは、起動後ずっと MOTOR_OFF 待機になる。**console の override (§3.3) でモードを指定して ETC を動かす** (Q4)。
- 同定実験は override で NORMAL 等を指定してから開始する (開始条件「モード ≠ MOTOR_OFF」)。
- telemetry の `EtcState.mode` は今どおり EtcTarget の実モードを送り、加えて `ControlStatus` で入力元と受信状態を出す (§3.4)。

## 6. 実装タッチポイント

| ファイル | 変更 |
|---|---|
| `src/control/control_input.{hpp,cpp}` | 新規 |
| `src/can/can_data.{hpp,cpp}` | `#if` を外し受信を常時有効化。`selectToEtcMode()` を削除 |
| `src/can/can_controller.cpp` / `can_bus.{hpp,cpp}` | 0x740 送信を削除 |
| `src/main.cpp` | ControlInput 生成、モード反映・アーミング③・autoOn の取得元を置換、`#if` 削除 |
| `src/etc/experiment_runner.{hpp,cpp}` | `ControlInput` を受け取り MOTOR_OFF 判定を置換 |
| `src/log_record_builder.{hpp,cpp}` | auto-shift ビットの取得元を置換、`#if` 削除 |
| `src/shift/auto_shifter.{hpp,cpp}` | I/O 層のパドル読み取り (`digitalRead(AUTO_SHIFT_UP/DOWN_IN_PIN)`) を `ControlInput` の 0x741 押下状態に置換。autoOn も `ControlInput` から。判断・出力は不変 |
| `src/can/can_data.{hpp,cpp}` (0x741) | Shift フレームのパースと 100 ms 途絶判定を追加 |
| `src/sensor/sensor_hub.{hpp,cpp}` | `modeSwitch` / `autoShiftSwitch` を削除 (Q6) |
| `src/constants.hpp` | `MODE_SELECT_SW_PIN_*` / `AUTO_SHIFT_SW_PIN` / `AUTO_SHIFT_UP/DOWN_IN_PIN` / `CONTROL_INPUT_VIA_CAN` を削除 (Q6・Q10) |
| `src/etc/plausibility_validator.hpp` | フラグ 9 個を private へ |
| `lib/kart-can` (別リポジトリ) | Control の送り手を変更し、サブモジュールを更新 |
| `CLAUDE.md` | CAN 節の「GPIO 直入力」記述を更新 |

PR #8 (実験モード修正) のマージ後に着手する (`experiment_runner` と `plausibility_validator` が重なるため)。

## 7. 未決事項

(Q1〜Q6 は §1 で決定済み)

- ~~Q7~~ (§1 で決定) **Q7 スコープ**: kart-can 307e038 の残り (0x741 Shift パドル、byte3 starter とセルモーターリレー、0x60A DC_Status 送信、0x604–0x609 送信、0x5F1 Vbat 受信) を本仕様に含めるか、別仕様に分けるか。推奨: 本仕様は 0x740 の mode / auto_shift 受信 + 0x740 送信の停止 + 0x60A (0x740 の後継として最低限の状態) まで。Shift と starter はアクチュエータ制御なので別仕様。Vbat は同定実験に要るので小さく別 PR で先行。
- **Q8 override の安全制約**: 「0x740 受信中は override 不可、受信開始で自動解除」で良いか。
- **Q9 override 中の MOTOR_OFF ラッチ**: override で MOTOR_OFF 以外を指定したとき、ETC は即動き出す。ベンチでは意図どおりだが、console に確認ダイアログを出すか。
- **Q10 GPIO パドル入力** (`AUTO_SHIFT_UP/DOWN_IN_PIN`): 0x741 に置き換わる予定だが、本仕様では残すか (Q7 と連動)。
- **Q11 kart-can サブモジュールの更新**: 1a01108 → 307e038。生成コードが 3,000 行規模で変わるので、本 PR の先頭コミットで更新しビルドが通ることを確認する。
- **Q12 起動直後の MOTOR_OFF 待機中の表示**: 0x60A / telemetry で「CAN 待ち」と分かるようにする (§3.4 で対応)。他に要る通知 (ダッシュ表示等) はあるか。

(以下、旧版の未決事項。§1 で決定済み)


- **Q1** 0x740 の送り手はどのノードか (ステアリング / ダッシュ等)。kart-can の変更は誰がいつ行うか。
- **Q2** GPIO 経路を完全に削除するか、ビルドフラグでフォールバックとして残すか (推奨: 削除。経路が 2 つあると今回のような分岐が残り続ける)。
- **Q3** 起動後 0x740 未受信の間の既定値を NORMAL / 手動シフトのままにするか、MOTOR_OFF (ETC 停止) で待つか。
- **Q4** ベンチでのモード切替は CANable2 から送る運用で良いか。送信用の小さなスクリプトを tools/ に置くか。
- **Q5** console に CAN 制御入力の受信状態 (`linkAlive`) を表示するか。
- **Q6** GPIO のノブ・スイッチのピン定義と読み取りコードを削除してよいか (配線も外す前提か)。
