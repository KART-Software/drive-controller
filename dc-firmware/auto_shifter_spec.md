# オートシフター仕様 (v1)

## 1. 概要とスコープ

クイックシフター/シーケンシャルミッションの UP/DOWN シフト信号を、ドライバー
入力ベース、または独自ロジックによる自動シフトで制御する機能。

ON/OFF は CAN で切り替える。

> **本機の役割は UP/DOWN パルスの生成・整形・中継**。クラッチ操作・点火カット・
> ブリッピング・オーバーレブ保護等のシフト機構制御は **IST コントローラ**が担う。
> 本機は「いつ UP/DOWN を打つか」と「どれだけの幅で打つか」だけを決める。

### v1 スコープ

| モード | シフト要求の発生源 | 出力 |
|---|---|---|
| **OFF (= manual)** | ドライバーの UP/DOWN 入力（立ち上がりエッジ） | 文脈に応じた幅の整形パルス（§4） |
| **ON (= auto)** | エンジン回転数・車輪速・ギアの独自ロジック（§7） | 同上 |

**重要**: 出力は **両モードとも「エッジ検出 → 整形パルス（ワンショット）」**で統一する。
OFF はレベルミラー（入力をそのまま垂れ流し）ではない。パルス幅を本機が
transmission/ギア/方向/停車状態に応じて制御する必要があるため（§4）。
OFF の「manual」とは「シフトの**判断**をドライバーが行う（本機は自動判断しない）」
意味であり、パルス整形は両モードで本機が行う。

### スコープ外 (将来)

- ON 時にドライバー入力と自動ロジックを融合するシフト判断（ブレンド制御）
- ギアポジションセンサによるシフト完了確認のクローズドループ
- シフト要求のキューイング（パルス中/クールダウン中の入力は v1 では破棄）

> **設計上の注意**: v1 では ON 中はドライバーの手動シフトが一切効かない。
> ドライバーが即座に手動へ戻せるよう、CAN の ON/OFF はコックピットスイッチ
> 等でドライバーが任意に切り替えられる前提とする（フェイルセーフは §6）。

---

## 2. GPIO / 電気仕様

新規 GPIO 4 本。

| 信号 | 方向 | 定数 (constants.hpp, 仮ピン) | 備考 |
|---|---|---|---|
| ドライバー UP 入力 | INPUT | `AUTO_SHIFT_UP_IN_PIN` | プルアップ前提（通常 HIGH、押下で LOW）。`ToggleSwitch` と同方針 |
| ドライバー DOWN 入力 | INPUT | `AUTO_SHIFT_DOWN_IN_PIN` | 同上 |
| UP 出力 | OUTPUT | `AUTO_SHIFT_UP_OUT_PIN` | アサートでシフトアップ要求 |
| DOWN 出力 | OUTPUT | `AUTO_SHIFT_DOWN_OUT_PIN` | アサートでシフトダウン要求 |

ピン極性は constants.hpp で定義（入力アクティブレベル / 出力アクティブレベルを
それぞれ `#define`）。実配線確定までは TODO ピンのプレースホルダ（F セクション
と同じ扱い）。

### 電気的注意

- 本機の GPIO はロジックレベル信号。シフトソレノイド等の誘導性負荷を直接駆動
  しないこと。出力先は IST コントローラのシフト入力 / 外部 MOSFET・リレー経由とする。
- 入力チャタリングは、出力状態機械（§5: パルス中/クールダウン中は新規要求を
  受けない）が自然に吸収する。バウンス時間（数 ms）≪ パルス幅（15〜100ms）の
  ため別途デバウンスは設けない。

---

## 3. 出力モデル（両モード共通）

シフト要求 `Dir ∈ {Up, Down}` を受けると、出力状態機械が 1 ショットの整形パルスを出す。

```
            requestDir (OFF=入力エッジ / ON=auto判断)
                 │  (OutState == Idle のときのみ受理)
                 ▼
   [Pulsing]  該当出力を pulseWidthFor() の幅だけアサート
                 │  幅経過
                 ▼
   [Cooldown] 両出力デアサート、cooldown_ms 待機
                 │  経過
                 ▼
   [Idle]     次の要求を受理可能
```

- `Pulsing` / `Cooldown` 中に来た要求は **破棄**（v1。キューイングは将来）。これが
  チャタリング・連打・モード切替時の暴発を防ぐ安全層を兼ねる。
- 入力エッジ追跡（`prevUpIn_`/`prevDownIn_`）は毎ティック更新し、立ち上がり検出は
  `Idle` のときのみ行動に移す。
- モード切替時（ON⇄OFF）は入力エッジ追跡を現在値で初期化し、切替直後に「押しっぱ」
  が誤発火しないようにする（新規押下のみ受理）。

---

## 4. パルス幅の決定

```
pulseWidthFor(gear, dir, moving):
    if transmission == IST:
        return ist_pulse_ms                 // 全シフト共通 (config)

    // transmission == NORMAL  (シフトパターン: 1 - N - 2 - 3 - 4 - 5 - 6)
    if !moving and ((gear == 1 and dir == Up) or (gear == 2 and dir == Down)):
        return normal_neutral_pulse_ms      // N に入れる短パルス (config)
    else:
        return normal_drive_pulse_ms        // N をスキップ/通常 (config)
```

- `moving = vWheelHz >= min_wheel_hz`（§7 の停止判定と同一しきい値）。
- **IST**（パターン N-1-2-3-4、N は最下段）: N が隣接ギア間に挟まらないので、全シフト
  共通の `ist_pulse_ms` でよい。
- **NORMAL**（パターン 1-N-2-3-4-5-6、N が 1 と 2 の間）:
  - **走行中**: 1↔2 を含む全シフトで `normal_drive_pulse_ms`（長め）を使い、N を
    通過して隣のギアまで押し込む。走行中に N に落ちないようにする。
  - **停車中の 1速→UP / 2速→DOWN**: `normal_neutral_pulse_ms`（短め）で N に入れる。
  - 停車中でも上記以外（例: N→UP で 2 へ、2→3 等）は `normal_drive_pulse_ms`。
- ギア不明（-1）時は安全側に `*_drive_pulse_ms`（NORMAL）/ `ist_pulse_ms`（IST）。

### パルス幅デフォルト（config、要実機調整）

| transmission | 項目 | 既定 |
|---|---|---|
| IST | `ist_pulse_ms` | 15 |
| NORMAL 走行中・通常 | `normal_drive_pulse_ms` | 100 |
| NORMAL 停車中 N 入れ | `normal_neutral_pulse_ms` | 25 |

---

## 5. シフト要求の発生源

`requestDir` の決定（`OutState::Idle` のときのみ）:

```
if !autoOn:                                 // OFF (manual)
    requestDir = pollDriverEdge()           // ドライバー判断。本機は整形のみ
else if stopped && gear in {N, 1, 2}:       // ON だが低速ギア帯で停車 → manual
    requestDir = pollDriverEdge()           // 1 / N / 2 を手動でナビゲート (auto 停止)
else:                                       // ON (auto)
    requestDir = evaluate()
```

> `stopped = vWheelHz < min_wheel_hz`。停車中の低速ギア帯（N・1速・2速）は
> **auto を停止してドライバーに渡す**。理由: §4 で「停車中 2速DOWN = N(25ms)」と
> 決めているため、ここで auto が 2→1 ダウンシフトすると意図と競合する。N の出入りは
> この帯でドライバーが管理する。

### OFF (manual)

- `UP_IN` / `DOWN_IN` の**立ち上がりエッジ**を検出（極性考慮）。
- `Idle` 中にエッジ → そのまま `requestDir` とする（本機は判断しない）。
- ギア範囲やしきい値でのブロックはしない（ドライバーの操作を尊重）。物理的に
  不可能なシフト（最下段で DOWN 等）は機構側が無視する。パルス幅選択のために
  ギア・車輪速は参照する（§4）。

### ON (auto)

- 通常はドライバー入力を**読まない / 無視**し、`evaluate()`（§7）に従う。
- **例外（manual フォールバック）**: **停車中（`vWheelHz < min_wheel_hz`）かつ gear ∈
  {N, 1速, 2速}** のときは auto を停止し、ドライバーのエッジを受理する。この帯では
  1速 / N / 2速 を手動でナビゲートできる:
  - N → 1速（N 脱出。NORMAL は DOWN、IST は UP）
  - 1速 → N（NORMAL は UP=25ms、IST は DOWN=`ist_pulse_ms`）
  - 2速 → N（NORMAL は DOWN=25ms。IST は N が隣接しないので 2→1→N と 2 タップ）
  - gear が 3 速以上に上がる / 走行を再開すると通常の auto に戻る。
- **減速して停車する場合の底**: 走行中（min_wheel_hz 以上）は auto がダウンシフトを
  続けるので、通常はスムーズに減速すれば停車時に 1速へ到達している。急停止で 2速の
  まま停車した場合は上記 manual 帯に入り、auto は 2→1 を行わない（ドライバーが
  2→N→1 等で操作）。3速以上で停車した場合は auto が 2速まで落としてから manual 帯へ。

---

## 6. 安全設計

`main.cpp` の tick で、ON を有効化する前に安全条件を AND する（launch の kill と同方針）:

```
effectiveAutoOn = canAutoShiftActive && plausibilityValidator.isCurrentlyValid()
```

`effectiveAutoOn == false` のとき OFF（manual）として振る舞う:

- プラウシビリティ違反時も manual は生き、ドライバーは手動シフト可能。
- CAN 断時も manual（§8 フォールバック）。

### `evaluate()` が `None` を返す（自動シフトしない）条件

`evaluate()` は §5 の auto 経路（manual 帯でない）でのみ呼ばれる。その上で:

- ギア不明（`getGear() == -1`）/ ニュートラル（gear == 0）— 自動で N を出入りしない
- UP: `gear < 1` / `gear >= maxGear`（最上段で UP しない。maxGear は IST=4 / NORMAL=6）
  / `vWheelHz < min_wheel_hz`（停車・極低速で UP しない）/ `apps < throttle_on_pct`（アクセルオフ中は UP しない）
- DOWN: `gear < 2`（1速・N・不明で DOWN しない）/ `apps >= throttle_on_pct`（アクセルオン中は DOWN しない）。
  **速度ゲートは適用しない**

> **ダウンシフトに速度ゲートを掛けない**理由: 減速中（走行中）に 1速まで自動的に
> 落とし切るため。停車後の低速ギア帯（N/1/2）は §5 で auto を停止し manual に渡すので、
> auto のダウンシフトが効くのは「走行中」か「停車中の 3速以上 → 2速まで」の範囲。
> アップシフトのみ停車・極低速で抑止する。

---

## 7. 自動シフトロジック (v1, ON モード)

`evaluate()` を `OutState::Idle` かつ `autoOn` のとき評価。

```
gear     = gps.getGear()
rpm      = engine.getFrequencyHz() / engine_teeth * 60   // RPM 換算
vWheelHz = (wheelFL.getFrequencyHz() + wheelFR.getFrequencyHz()) / 2
apps     = apps1.convertedValue()                        // ドライバーのアクセル開度 (0-100%)

§6 抑止条件のいずれか成立 → None
UP   条件: rpm >= upshift_rpm   かつ 1 <= gear < maxGear かつ vWheelHz >= min_wheel_hz
                                かつ apps >= throttle_on_pct   (アクセルオン)
DOWN 条件: rpm <= downshift_rpm かつ gear >= 2 かつ apps < throttle_on_pct  (アクセルオフ)
UP と DOWN が同時成立した場合は UP 優先
```

### スロットルゲート（アクセル開度による up/down 振り分け）

- **UP はアクセルオン（`apps >= throttle_on_pct`）時のみ**: 加速中に上げる。惰行・
  ブレーキング中の意図しない UP を防ぐ。
- **DOWN はアクセルオフ（`apps < throttle_on_pct`）時のみ**: 減速・ブレーキング中に
  下げる。加速中の一時的な回転落ちで誤 DOWN しない。
- **ハンチング根治**: アップシフト直後はアクセルオン → DOWN がゲートで禁止されるため、
  「アップ → 回転落ち → 即ダウン → …」の振動が原理的に起きない（単一 RPM しきい値でも安全）。
- `throttle_on_pct` は config（既定 50%、要実機調整）。APPS は `apps1` を使用。
- 減速して停車する流れ（§6 の「1速まで落とし切る」）はアクセルオフ＝ DOWN ゲート ON
  なので整合する。

### v1 ロジックの割り切り

- オープンループ（パルスを出したら成功と見なし、cooldown で次まで待つ）。
- シフト完了をギアセンサで確認しない（将来クローズドループ化）。
- 単一の up/down RPM しきい値 + 単一スロットルしきい値（per-gear マップ・ヒステリシス帯は将来）。
- オーバーレブ予測・ブリッピング・空転ガードは行わない（IST コントローラ責務 / 将来）。

---

## 8. CAN プロトコル

### 受信: ON/OFF 指令

| 項目 | 値 |
|---|---|
| CAN ID | `CAN_ID_AUTO_SHIFT` = **0x400**（提案。0x200=MODE, 0x300=LAUNCH に続く番号） |
| byte0 | `0x01` = ON、それ以外 = OFF |
| 周期 | 上位 ECU が定期送信（ハートビート兼用） |

`CanRxData` に追加:

```cpp
bool autoShiftActive = false;
unsigned long lastAutoShiftFrameMs = 0;
```

- `mergeFrame()`: `CAN_ID_AUTO_SHIFT` 受信時に `autoShiftActive = (buf[0]==0x01)`、
  `lastAutoShiftFrameMs = millis()`。
- `checkTimeouts()`: `CAN_AUTO_SHIFT_TIMEOUT_MS`（= 200ms、他と同値）以上途絶
  したら `autoShiftActive = false`（= OFF = manual）にフォールバック。

> **フォールバック方針**: CAN 断 → OFF（手動）。ドライバーが直接シフトを制御
> できる状態が最も安全なため。MOTOR_OFF のようなラッチはしない。

---

## 9. Config (proto)

`Config` にトップレベルメッセージ `AutoShiftConfig` を追加（`overlayConfig()` に
1 行追加が必要 — CLAUDE.md の不変条件参照）。

```proto
message AutoShiftConfig {
  float  upshift_rpm             = 1;  // UP しきい値 (RPM)
  float  downshift_rpm           = 2;  // DOWN しきい値 (RPM)
  float  min_wheel_hz            = 3;  // 停止/走行 境界 (兼 ON 時の停車抑止しきい)
  uint32 cooldown_ms             = 4;  // シフト間ロックアウト (両モード)
  uint32 ist_pulse_ms            = 5;  // IST: 全シフト共通パルス幅
  uint32 normal_drive_pulse_ms   = 6;  // NORMAL 走行中/通常: N スキップ
  uint32 normal_neutral_pulse_ms = 7;  // NORMAL 停車中 1速UP/2速DOWN: N 入れ
  float  throttle_on_pct         = 8;  // この APPS% 以上=アクセルオン(UP可) / 未満=オフ(DOWN可)
  // 将来: repeated float per_gear_upshift_rpm / ヒステリシス帯 など
}

message Config {
  // ... 既存 ...
  AutoShiftConfig auto_shift = 5;  // 次の空き番号
}
```

- transmission 種別（IST/NORMAL）は既存 `SensorCalib.gps.type` を流用。`setConfig` に
  渡す。
- engine_teeth（RPM 換算用）も既存 `SensorCalib.engine_teeth` を `setConfig` に渡す。
- `Configurator::calibrate()`: `if (config.has_auto_shift) autoShifter.setConfig(config.auto_shift, gps.type, engine_teeth)`。
- `Configurator::overlayConfig()`: `if (src.has_auto_shift) config.auto_shift = src.auto_shift;`
- デフォルト値は `default_config.hpp` に追記（パルス幅は §4 の表、RPM は要実機調整）。

---

## 10. ソフトウェア構成

新規 namespace `shift::`（`etc::`, `launch::` と同方針）。

```
dc-firmware/src/shift/
  auto_shifter.hpp / .cpp
```

### 層構成（責務分離）

上から policy、下から hardware。`update()` は ②→（①）→③→④ の順に流れる。

```
 ┌─ [① 判断ロジック層]  evaluate(snapshot) → {Up, Down, None}
 │     RPM / 車輪速 / ギア / APPS のしきい値判断。状態を持たない純粋判断。
 │     唯一ドメイン知識（シフトの賢さ）を持つ層 = 将来の拡張点。
 │     (per-gear マップ / 空転ガード / BPS 連動 / ブレンド制御 はすべてここに入る)
 │          ▲ ON のときだけ呼ばれる
 │          │
 ├─ [② 調停層 arbitration]  requestDir をどこから取るか決める (§5)
 │     OFF / ON+停車+低速ギア帯(N/1/2) → ④の pollDriverEdge() (ドライバー判断)
 │     ON & それ以外                  → ① evaluate()          (auto 判断)
 │          │
 │          ▼
 ├─ [③ 出力整形層]  pulseWidthFor() (§4) + 状態機械 Idle→Pulsing→Cooldown (§3)
 │     requestDir を 1 ショットの整形パルスにして GPIO へ。連射防止もここ。
 │          │
 │          ▼
 └─ [④ I/O 層]  pollDriverEdge(入力GPIO エッジ検出) / センサー getter / digitalWrite(出力GPIO)
```

- **② 調停と ① 判断は別物**: 「誰が決めるか（mode/状態）」と「auto が何を出すか（policy）」を
  分離する。①③④は OFF/ON で共通で、モード差は②の分岐だけ。
- **① 判断ロジック層は副作用なし・状態を持たない純粋判断**に保つ（入力 = センサー
  スナップショット、出力 = `{Up,Down,None}`）。v1 では `AutoShifter::evaluate()` メソッド
  として実装するが、将来 `ShiftPolicy` 等へ切り出して単体テスト・ブレンド制御の追加が
  しやすいよう、この性質を維持する。
- **将来の「賢さ」は①に集約**する。②③④は安定層。

### クラス定義

```cpp
namespace shift {

class AutoShifter {
   public:
    AutoShifter(const PulseCounter& engine,
                const PulseCounter& wheelFL,
                const PulseCounter& wheelFR,
                const GearPositionSensor& gps,
                const Apps& apps);

    void begin();                          // pinMode 設定、出力を非アサート初期化
    void setConfig(const dc_AutoShiftConfig& cfg, dc_TransmissionType tx, uint32_t engineTeeth);
    void update(bool autoOn);              // 毎ティック呼ぶ

    enum class State { Idle, Pulsing, Cooldown };

   private:
    enum class Dir { None, Up, Down };

    // -- 入力センサー (SensorHub 全体ではなく必要分だけ const 参照) --
    const PulseCounter& engine_;
    const PulseCounter& wheelFL_;          // 非駆動輪 = 真の車速 (ホイールスピン非依存)
    const PulseCounter& wheelFR_;
    const GearPositionSensor& gps_;
    const Apps& apps_;                     // ドライバーのアクセル開度 (スロットルゲート)

    // -- config --
    float upshiftRpm_, downshiftRpm_, minWheelHz_, throttleOnPct_;
    uint32_t cooldownMs_;
    uint32_t istPulseMs_, normalDrivePulseMs_, normalNeutralPulseMs_;
    dc_TransmissionType tx_;
    uint32_t engineTeeth_ = 1;
    int8_t maxGear_ = 4;                   // tx から決定 (IST=4 / NORMAL=6)

    // -- 出力状態機械 --
    State state_ = State::Idle;
    unsigned long stateEnteredMs_ = 0;
    uint32_t activePulseMs_ = 0;
    bool prevUpIn_ = false, prevDownIn_ = false;
    bool prevAutoOn_ = false;

    // -- helpers --
    bool readUpIn() const;                 // 極性考慮 digitalRead
    bool readDownIn() const;
    Dir pollDriverEdge();                  // 立ち上がり検出 (OFF)
    Dir evaluate() const;                  // 自動判断 (ON)
    bool moving() const;                   // vWheelHz >= minWheelHz_
    uint32_t pulseWidthFor(int8_t gear, Dir dir) const;
    void startPulse(Dir dir);
    void writeOutputs(bool up, bool down);
    float engineRpm() const;
};

}  // namespace shift
```

---

## 11. 実行コンテキスト / タイミング

`AutoShifter::update()` は `loop()` から**毎イテレーション**呼ぶ（インターバル
ゲートなし）。入力エッジ取りこぼし防止とパルス計時のため。

- パルス幅・クールダウン・状態遷移は内部で `millis()` 計時。
- `evaluate()` 入力（RPM 100ms 更新、ギア）は低速データなので評価頻度は問題にならない。
- launch FSM tick / `CAN_TX`(16ms) / `PULSE_UPDATE`(100ms) のどの周期にも相乗りしない。
- **代替案**: 決定論的レイテンシが必要になれば専用 `IntervalTimer` 化。ただし ISR から
  `MovingAverage::getAvg()` 系（`noInterrupts()` 内包）を呼ばないこと。v1 は loop() で十分。

---

## 12. main.cpp 配線（実装イメージ）

```cpp
shift::AutoShifter autoShifter(sensorHub.pulseEngine(),
                               sensorHub.pulseWheelFL(),
                               sensorHub.pulseWheelFR(),
                               sensorHub.gps(),
                               sensorHub.apps1());
// setup(): autoShifter.begin();  Configurator が setConfig を反映
// loop() (毎イテレーション):
    bool autoOn = canController.rxData().autoShiftActive
               && plausibilityValidator.isCurrentlyValid();
    autoShifter.update(autoOn);
```

`Configurator` に `AutoShifter&` を注入し、`calibrate()` で `setConfig` を反映
（launch と同パターン）。

---

## 13. オブザーバビリティ

- 現在モード（OFF/ON）・出力状態・直近シフト（方向・時刻）を確認できるようにする。
- v1: 最低限 `SerialProtocol::sendDebugf` でモード遷移・シフトイベントを出力。
- 推奨: proto に `AutoShiftState { mode; out_state; last_dir; cooldown_remaining }`
  を追加し `dc_State` で送信（Web Console 監視）。launch の observability と同様。
- **素の `Serial.printf` は使わない**（COBS フレーム破壊）。

---

## 14. 将来スコープ

| # | 項目 |
|---|---|
| 1 | ON 時のドライバー入力 × 自動ロジックのブレンド制御 |
| 2 | per-gear の up/down RPM マップ + スロットルしきい値のヒステリシス帯 |
| 3 | ホイールスピン・ガード（前輪 vs 駆動輪比較で空転中の誤 UP 抑止） |
| 4 | ブレーキ（BPS）連動のダウンシフト判断 |
| 5 | ギアセンサによるシフト完了確認（クローズドループ、失敗時リトライ/フラグ） |
| 6 | シフト要求のキューイング（パルス/クールダウン中の入力を保持） |
| 7 | ON 中の N 投入（ギア→N）対応（v1 は N 脱出のみ。N 投入は OFF で行う） |
| 8 | proto `AutoShiftState` 送信による Web Console 監視 |

> オーバーレブ保護・ブリッピング等のシフト保護制御は IST コントローラ責務のため
> 本機の将来スコープからも除外。

---

## 15. 実装チェックリスト

- [ ] `constants.hpp`: 4 ピン + 極性 + `CAN_ID_AUTO_SHIFT`/`CAN_AUTO_SHIFT_TIMEOUT_MS`（パルス幅は config 化のため定数なし）
- [ ] `spec/proto/drive_controller.proto`: `AutoShiftConfig`（8 フィールド、`throttle_on_pct` 含む）追加 → 両側 regen
- [ ] `can/can_data.{hpp,cpp}`: `autoShiftActive` / `lastAutoShiftFrameMs` + merge/timeout
- [ ] `sensor/sensor_hub.hpp`: `pulseWheelFL()` / `pulseWheelFR()` / `gps()` / `apps1()` の const 参照（いずれも既存）
- [ ] `shift/auto_shifter.{hpp,cpp}`: 本体実装（両モード整形パルス + パルス幅 + スロットルゲート）
- [ ] `configurator.{hpp,cpp}`: `AutoShifter&` 注入 + calibrate（tx/engine_teeth 渡し）+ overlayConfig 1 行
- [ ] `default_config.hpp`: デフォルト値（パルス幅 15/100/25、throttle_on_pct 50、RPM/cooldown 暫定）
- [ ] `main.cpp`: インスタンス生成 + begin + loop tick（毎イテレーション、安全 AND 付き）
- [ ] (任意) `AutoShiftState` proto + シリアル送信
