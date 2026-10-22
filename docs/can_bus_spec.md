# CAN バス: インターフェースと FlexCAN3 ドライバ 仕様書

Status: **実装済み・ベンチ確認済み (2026-10-20)。ブランチ feature/can-driver (feature/loop-phase3 の上)。**
関連: CLAUDE.md「CAN」節、`dc-firmware/dc-main/lib/kart-can` (フレームの定義)、[loop_nonblocking_spec.md](loop_nonblocking_spec.md) §6.3 (受信割り込み)、
参考資料 `local/IMXRT1060RM_rev3.pdf` Chapter 45 (CANFD/FlexCAN3。Teensy 4.1 の CAN3 はこのモジュール)。

## 1. 目的と背景

FlexCAN_T4 (submodule) をやめ、CAN を**インターフェース (`CanBus`) + 自前の FlexCAN3 ドライバ**にする。

2026-10-20 のベンチで分かったこと:
- FlexCAN_T4 はヘッダ内の `static` (`_CAN3`、`flexcan_isr_can3`) をファイルごとに別物として持ち、コンストラクタと `begin()` が
  別の翻訳単位の実体にリンクされると ISR が null を見て何も処理せず、受信フラグが消えずに割り込みが無限に再入する
  (loop・USB・サンプリングが止まる)。`FlexCAN_T4.h` は `can_data.hpp` 経由で 11 の翻訳単位が include していた。
- ほかにも `setBaudRate()` が黙ってエラー割り込みを有効にする、`write(mb, msg)` の送信完了処理で IFLAG を消し忘れる経路、
  コールバックの文脈が `events()` の有無で暗黙に変わる、など。使う機能は全体の 1 割程度。
- 性能は決め手ではない (受信割り込み 1 回 1.7 µs、送信 4 フレーム 4.9 µs、CPU 0.07 %)。自前にする理由は「読めること・
  決められること」(割り込みの文脈、エラー時の挙動、最悪値)。

## 2. 範囲

使う機能だけを実装する。
- 標準 ID (11 bit) のデータフレームのみ。拡張 ID・リモートフレーム・CAN FD は扱わない (受信フィルタで落とす)。
- 送信: 周期送信 (0x600〜0x603 を 16 ms、0x60A を 33 ms。tickISR から)。空きメールボックスがなければ**捨てて false** (次の周期で送る。
  キューは持たない)。
- 受信: ID ごとに受信メールボックスを 1 本 (0x740、0x741。個別マスクで一致)。受信は**割り込みの文脈でコールバック**を呼ぶ
  (0x741 を受けた瞬間にシフターへ渡す今の設計のまま)。Rx FIFO は CAN FD で使えないので使わない (§4.8)。
- エラー: 状態 (error active / passive / bus-off、TEC / REC) を読める。バスオフは FlexCAN の自動復帰 (`BOFF_REC` = 0) に任せる。
  エラー割り込みは使わない (エラーごとの割り込みは要らず、ESR1 のフラグは受信・送信の割り込みの末尾で消す)。
- 計測: 受信数、受信メールボックスの取りこぼし (overrun) 回数、送信の捨て回数、割り込み回数。
- CAN FD: 今は有効にしないが、構造は FD に備える (§4.8)。

## 3. インターフェース

`src/interfaces/can/can_frame.hpp` (フレーム) と `can_bus.hpp` (バス)。`can_data` はフレームだけを、`CanController` はバスを知る。
どちらもハードウェアもライブラリも知らない。

```cpp
// kart-can の pack / unpack と同じ「ID + 長さ + バイト列」。FD に備えてデータは 64 B まで (classic では len ≤ 8)
struct CanFrame {
    uint32_t id = 0;        // 標準 ID (0x000〜0x7FF)
    uint8_t len = 0;        // classic 0〜8、FD 0〜64
    bool fd = false;        // CAN FD フレーム (EDL)
    bool brs = false;       // FD のデータ相で速度を上げる
    uint8_t data[64] = {};
};

struct CanConfig {
    uint32_t bitrate;       // アービトレーション相 [bit/s]
    bool fd = false;        // 今の実装は非対応 (begin() が false を返す)
    uint32_t dataBitrate = 0;
};

struct CanState {
    enum class Fault : uint8_t { Active, Passive, BusOff };
    Fault fault;        // ESR1 FLTCONF
    uint8_t txErrors;   // ECR TXERRCNT
    uint8_t rxErrors;   // ECR RXERRCNT
    bool synced;        // バスに同期している (ESR1 SYNCH)
};

struct CanStats {
    uint32_t rxFrames;    // コールバックに渡したフレーム数
    uint32_t rxOverrun;   // 受信メールボックスを読む前に次が来て、前のフレームを失った回数 (CODE OVERRUN)
    uint32_t txDropped;   // 空きメールボックスがなく捨てた数
    uint32_t isrCount;    // 割り込みの回数
};

class CanBus {
   public:
    virtual ~CanBus() = default;
    // onRx は受信割り込みの文脈で呼ばれる (短く。Serial / SD / malloc 禁止)。filters は受け入れる標準 ID (最大 8)。
    // false = 対応していない設定 (FD、表にないビットレート)
    virtual bool begin(const CanConfig& config, const CanRxFilters& filters, void (*onRx)(const CanFrame&), uint8_t irqPriority) = 0;
    virtual bool send(const CanFrame& frame) = 0;  // 空きメールボックスに書く。なければ false (捨てる)
    virtual CanState state() const = 0;
    virtual CanStats stats() const = 0;
};
```
- `CanRxFilters` は固定容量のリスト (`std::array<uint32_t, kMax = 8>` + 個数)。ID ごとに受信メールボックスを 1 本使う
  (ドライバの受信メールボックス数と `static_assert` でつないである)。
- ビットレートは `CAN_BITRATE` (constants.hpp、kart-can の `can.yaml` と同じ 1 Mbit/s) を `CanController` が `CanConfig` で渡す。
  ドライバはビットタイミングの表を持ち、表にない値や FD には `false` を返す。
- Zephyr の `drivers/can.h` の最小サブセット (`can_frame` / `can_send` / `can_add_rx_filter` / `can_get_state`) を手本にした。

## 4. FlexCAN3 ドライバ (`src/can/flexcan3_bus.{hpp,cpp}`、`class Flexcan3Bus final : public CanBus`)

Teensy 4.1 の CAN3 (pin 30 = CRX3、pin 31 = CTX3)。単一インスタンス (ISR から届くための静的ポインタは **.cpp の中**に置く)。
レジスタは RM Chapter 45 のオフセットを自前の定義で持つ (`FLEXCAN3` ベース 0x401D8000。FlexCAN_T4 の `imxrt_flexcan.h` は使わない)。

| オフセット | レジスタ | 使い方 |
|---|---|---|
| 0x00 | MCR | モジュール有効 / freeze / FIFO 有効 (RFEN、ライブラリでは FEN) / SRXDIS / IRMQ / WRNEN / MAXMB |
| 0x04 | CTRL1 | ビットタイミング、CLKSRC、BOFFREC、エラー割り込みマスク (使わない = 0) |
| 0x08 | TIMER | 読むとメールボックスのロックを解除する |
| 0x1C | ECR | TXERRCNT / RXERRCNT |
| 0x20 | ESR1 | 状態とエラーフラグ (w1c) |
| 0x28 | IMASK1 | 割り込みマスク (受信メールボックスのビットだけ) |
| 0x30 | IFLAG1 | 割り込みフラグ (w1c。bit n = メールボックス n。送信完了のフラグは割り込みにしない) |
| 0x34 | CTRL2 | EACEN (マスクの bit 31 / 30 で RTR / IDE も比較) |
| 0x10 / 0x48 | RXMGMASK / RXFGMASK | (IRMQ = 1 なので RXIMR を使う。同じ値を入れておく) |
| 0x80 + 16n | MB n: CS / ID / WORD0 / WORD1 | n = 0〜7: 受信 (フィルタの数だけ)、8〜23: 送信 (16 本)。16 B は classic の値 (§4.8) |
| 0x880 + 4n | RXIMR n | 受信メールボックス n のマスク |

### 4.1 クロックとピン
- CAN のクロック: `CCM_CSCMR2` の CAN_CLK_SEL = 1 (24 MHz 水晶)、CAN_CLK_PODF = 0。クロックゲート: `CCM_CCGR7` の CAN3 (bits 6〜9)。
  CTRL1 の CLKSRC = 0 (発振器クロックを PE に)。
- ピン: `IOMUXC_SW_MUX_CTL_PAD_GPIO_EMC_36` = ALT9 | SION (pin 31、TX)、`..._EMC_37` = ALT9 | SION (pin 30、RX)、
  `IOMUXC_CANFD_IPP_IND_CANRX_SELECT_INPUT` = 0 (EMC_37 を選ぶ)。PAD_CTL は 0x10B0 (キーパー、DSE 6、SPEED 2)。
  (`constants.hpp` の `CAN3_RX_PIN 30` / `CAN3_TX_PIN 31` で宣言し、実配線と照合できるようにする。)

### 4.2 ビットタイミング (1 Mbit/s)
今 (FlexCAN_T4) と同じ値にする: CTRL1 = 0x01620002 → PRESDIV = 1 (24 MHz ÷ 2 = 12 MHz、Tq = 83.3 ns)、PROPSEG = 2 (3 Tq)、
PSEG1 = 4 (5 Tq)、PSEG2 = 2 (3 Tq)、RJW = 1 (2 Tq)。合計 1 + 3 + 5 + 3 = 12 Tq、サンプル点 9 / 12 = 75 %。
`CAN_BITRATE` から `static_assert` で 1 Mbit/s であることを確かめる (別のビットレートに対応するときは表を足す)。

### 4.3 初期化 (RM §45.5.1 の順)
1. クロックとピン (§4.1)。MCR の MDIS を下ろしてモジュールを有効化。
2. freeze: MCR |= FRZ | HALT、FRZ_ACK を待つ。SOFT_RST を立てて下りるのを待つ (再び FRZ_ACK を待つ)。
3. MCR: SRXDIS (自己受信なし)、IRMQ (個別マスク)、WRNEN (TXWRN / RXWRN のフラグ)、MAXMB = 15 (MB 0〜15)、
   RFEN = 0 (FIFO は使わない)、DMA = 0、FDEN = 0。
4. CTRL1: §4.2 のタイミング + BOFFREC = 0 (自動復帰)。ERRMSK / BOFFMSK / TWRNMSK / RWRNMSK = 0。
5. CTRL2: EACEN = 1 (IDE / RTR も比較)。
6. メールボックス: IMASK1 = 0 にして MB 0〜15 の CS / ID / WORD と RXIMR を 0 に。IFLAG1 を全部消す (読んで書き戻す)。
   (Rx FIFO を使う場合は、RFEN を立てる前にこのクリアを行うこと。立てた後に FIFO の予約領域 0x90〜0xDC へ書くと CPU が止まる。)
7. 受信メールボックス n (フィルタの ID ごと): ID = id << 18、RXIMR n = 0xDFFC0000 (bit 31 RTR、30 IDE、28〜18 ID を全部比較)、
   CS = RX_EMPTY (0b0100)。送信メールボックス 8〜23: CS = TX_INACTIVE (0b1000)。
8. IMASK1 = 受信メールボックスのビットだけ (送信完了の割り込みは使わない)。
9. `attachInterruptVector(IRQ_CAN3, isr)`、`NVIC_SET_PRIORITY(IRQ_CAN3, irqPriority)`、`NVIC_ENABLE_IRQ`。
10. MCR の HALT (と FRZ) を下ろして freeze を抜け、FRZ_ACK が下りるのを待つ。以後 FlexCAN は 11 bit の recessive を待って同期する。

### 4.4 送信 (`send`、tickISR から)
classic で扱えないフレーム (`fd` または `len > 8`) は黙って丸めずに `txDropped++` して false。
MB 8〜23 を順に見て CS の CODE が TX_INACTIVE (0b1000) のものを使う。なければ `txDropped++` して false。
送信メールボックスは 16 本: 1 tick に書く最大は 5 + 1 フレーム (0x604 系 5 本と 0x60A が重なる tick)、1 Mbit/s の classic 8 B は 1 フレーム約 130 µs なので
普段は次の tick までに全部出るが、他ノードの送信で待たされても 2 tick 分は溜められる (2026-10-21、0x604〜0x609 の追加で 8 → 16)。
書く順: IFLAG1 の該当ビットを消す (前回の完了フラグ) → CS = INACTIVE → ID = id << 18 → WORD0 / WORD1 (データをビッグエンディアンで詰める)
→ CS = (len << 16) | CODE TX_ONCE (0b1100)。完了すると CODE は INACTIVE に戻り IFLAG1 のビットが立つ (割り込みは出さない。
次の `send` が消す)。

### 4.5 受信割り込み (`isr`、優先度 64 = tickISR と同じ)
1. IFLAG1 を読み、受信メールボックスのビットだけ見る。
2. 立っているメールボックス n ごとに: CS を読む (ロックされる)。CODE の BUSY ビットが立っていれば (書き込み中、数十クロック)
   上限付きで読み直し、それでも BUSY ならそのまま残して次へ (フラグが残るので戻ったあと再び割り込む)。
   FULL (0b0010) か OVERRUN (0b0110) なら ID / WORD0 / WORD1 を読み、**IFLAG1 = そのビットだけを書いて消し** (読み書きで他の
   ビットを消さない。RM §45.3.12 の注意)、CS = RX_EMPTY を書いて次を受けられるようにし、TIMER を読んでロックを解除する。
   **フラグを消すのはロック解除の前**: 解除の瞬間に、ロック中に届いた次のフレームが移ってきてフラグが立つので、解除後に消すと
   そのフラグを消してしまい、次が来るまで読まれないフレームが残る (2026-10-20 のレビューで修正)。
   OVERRUN なら `rxOverrun++` (読む前に次が来て前のフレームを失った)。`CanFrame` に詰めて (len = DLC、id = ID >> 18)
   `rxFrames++`、コールバックを呼ぶ。
3. 末尾で ESR1 を読み、w1c のフラグ (ERRINT / BOFFINT / TWRNINT / RWRNINT / WAKINT / BOFFDONEINT / ERRINT_FAST / ERROVR) を書き戻して消す。
   `isrCount++`。`dsb`。
メールボックスは ID ごとに 1 フレームしか保持しないので、同じ ID が ISR の処理 (1 µs 程度) より速く連続すると前のフレームを失う
(OVERRUN として数える)。0x741 の「周期 + 変化時の即時 1 フレーム」(最短約 100 µs 間隔) は十分に間に合う。

### 4.6 状態 (`state`、どの文脈からでも)
ESR1 の FLTCONF (bits 5:4: 00 = active、01 = passive、1x = bus-off)、SYNCH (bit 18)、ECR の TEC (bits 7:0) / REC (bits 15:8) を読んで返す。
bus-off の間、TEC には復帰の進み (11 bit の recessive を見た回数 0〜127) が入る (ベンチで観測、§6.1)。

### 4.7 作らないもの
送信キュー、送信完了割り込み、Rx FIFO、拡張 ID、リモートフレーム、DMA、リッスンオンリー、ループバック、タイムスタンプ、
エラー割り込み。必要になったときに足す。

### 4.8 CAN FD への備え (段階 1 = 構造だけ。2026-10-20 決定)
Teensy 4.1 で FD ができるのは CAN3 だけで、このドライバはそれを直接扱っている。FD にするときに変わるものと、今の時点で済ませたもの:
- **受信は Rx FIFO が使えない** (RM の注意: "FIFO cannot be enabled when CAN FD feature is enabled"。RT1060 に Enhanced Rx FIFO はない)
  → 済み: 受信はメールボックス (ID ごとに 1 本)。
- **フレーム** → 済み: `CanFrame` は `data[64]` と `fd` / `brs` を持つ。classic では `len ≤ 8`、フラグは false。
- **設定の入口** → 済み: `CanConfig { bitrate, fd, dataBitrate }`。`fd = true` は `begin()` が false を返す。
- **メールボックスの配置** → 済み: ストライド `kMbBytes` (classic 16 B) を定数にした。FD で 64 B にすると 72 B になり、MB 数も減る。
- 未: MCR FDEN、CBT (BTF = 1、アービトレーション相を拡張レンジで)、FDCBT (データ相)、FDCTRL (FDRATE / MBDSR / TDC)、
  CAN クロックの 80 MHz 化、DLC 9〜15 ↔ 12〜64 B の換算、送受信のデータ 64 B 対応。150〜200 行の見込み。
  ループバックモードで自己検証はできるが、実バスの検証には FD の相手 (CANable を candlelight-FD に、または Debix) が要る。
  kart のバス全体を FD にする決定が先。

## 5. 統合

| 変更 | 内容 |
|---|---|
| `can_data.{hpp,cpp}` | `CAN_message_t` → `CanFrame`。`toFrames()` / `toFrame()` / `mergeFrame()` / `needsImmediateHandling()` の型を変える。`<FlexCAN_T4.h>` の include を消す (これで `control_input.hpp` 以下がライブラリから切れる) |
| `can/can_bus.{hpp,cpp}` | 削除。役割は `interfaces/can/can_bus.hpp` (interface) と `can/flexcan3_bus.{hpp,cpp}` (実装) へ |
| `can/can_controller.{hpp,cpp}` | `CanBus&` を受け取る (main が `Flexcan3Bus` を作って渡す)。`send()` は `CanImuData::toFrames()` → `bus.send()` を 5 回。`receive()` は `CanFrame` を受ける。`rxTotal` は `bus.stats().rxFrames` に |
| `main.cpp` | `Flexcan3Bus canBus; CanController canController(canBus);`。`canRxISR(const CanFrame&)`。`begin()` にフィルタ {0x740, 0x741} と `CAN_RX_ISR_PRIORITY` |
| センサーの送信 (2026-10-21) | `CanSensorData` (`can_data`) + `CanController::sendSensors()`: 0x604 DC_Pedals (APPS1 / TPS1 / クラッチ %、後ろブレーキ psi)、0x605 DC_EtcRaw (APPS / TPS 各 2 系統の ADS8688 カウント)、0x607 DC_WheelSpeed (km/h は車輪センサー `WheelSpeedSensor::kmh()` から。タイヤ直径はセンサーが持ち、未設定なら 0 km/h)、0x608 DC_WheelPulse (累計パルス数 `RotationSensor::pulses()` を uint16 でラップ)、0x609 DC_Engine (エンジン / クラッチ後 rpm)。16 ms 周期で 0x600 系と半周期ずらす (`n % 16 == 8`)。0x606 DC_StrokeRear は 2 台目の ADC が未接続なので送らない。物理値 → uint16 は `scaled()` で丸めて飽和 (負と NaN は 0) |
| telemetry / console | `Sensor.can` (`CanStatus`: fault、TEC / REC、受信・取りこぼし・捨ての累計) を 50 Hz の State に同梱。console は Control Input パネルに「CAN バス: 正常 (受信 n フレーム/s, TEC) / 警告 (passive) / バスオフ」、画面下の sys-stats 行に数値 (`CAN active rx n/s overrun txdrop TEC REC`)。fault と TEC で「バス自体の問題 (passive / bus-off)」と「バスは生きているが送り手が黙っている (active・TEC 0・受信 0)」を見分ける。bench 専用の debug 行と遮断器は削除 (2026-10-20 レビュー 6) |
| `lib/FlexCAN_T4` | 検証が終わったら submodule を外す (`.gitmodules`、`lib/`)。`lib/kart-can` はそのまま |
| ドキュメント | CLAUDE.md の CAN 節 (ドライバの構成、ISR の文脈、禁止事項)、本書の Status |

割り込みの文脈は今と同じ: `canRxISR` は CAN3 の割り込み (優先度 64) から呼ばれ、tickISR と同じ優先度なので互いに割り込まない。
`ControlInput` / `AutoShifter::onPaddles()` の扱いは変わらない。

## 6. 検証 (ベンチ: M5Stack Mini CAN Unit、CANable2 + can-mock-py、Debix)

| 項目 | 期待 |
|---|---|
| 受信 | can-mock-py の 0x5F0〜0x5F4 は**フィルタで落ちる** (`rxFrames` が増えない。FlexCAN_T4 のときは 145 回/s 増えていた)。CANable から 0x740 / 0x741 を送ると `rxFrames` が増える |
| 送信 | `candump` で 0x600〜0x603 が 16 ms、0x60A が 33 ms、エラーフレームなし (今と同じ) |
| 通し | 0x740 NORMAL → ETC NORMAL、途絶 200 ms → NORMAL 維持、MOTOR_OFF → ラッチ (今日と同じ手順) |
| 0x741 | CANable から up を送ると、受信割り込みの中で `onPaddles()` が呼ばれてパルスが出る (`autoshift_state` が telemetry / ログで変わる) |
| バス故障 | RX (白い線) を抜く → `state()` が passive → bus-off、Teensy は止まらない (telemetry 50 Hz)。戻すと自動復帰して送受信が再開 |
| 割り込みの嵐 | 起きない (遮断器が働かない)。FIFO あふれは、CANable から 0x740 を連射 (例: 1000 フレーム/s) しても `rxOverflow` = 0 |
| 所要 | 受信割り込み 1 回 ≤ 2 µs、送信 5 フレーム ≤ 5 µs (bench で DWT を使って一時計測)。tick の所要が今 (18〜20 µs) より増えない |
| 翻訳単位 | `FlexCAN_T4.h` を include するファイルが 0 になる (include の走査スクリプトで確認) |

## 6.1 ベンチの結果 (2026-10-20)

| 項目 | 結果 |
|---|---|
| フィルタ | can-mock-py の 0x5F0〜0x5F4 (約 145 回/s) では受信数も割り込みも増えない (Rx FIFO 版・メールボックス版とも) |
| 送信 | `candump` 4 s: 0x600〜0x603 各 250 回 (16 ms)、0x60A 121 回 (33 ms)、エラーフレーム 0。送信の捨て 0 |
| 通し | 0x740 NORMAL → ETC NORMAL、途絶 218 ms → NORMAL 維持、MOTOR_OFF → ラッチ |
| 0x741 | 受信割り込みから `onPaddles()` → `autoshift: UP` のパルス。shift link alive |
| 連射 | 0x740 を 1,000 フレーム連射 (cansend、約 1 ms 間隔) → 受信 1,120 = 送った数ちょうど、取りこぼし 0、割り込み 1,120 回 (メールボックス受信でも同じ) |
| 所要 | tick の最大 20 → 16 µs (送信 5 フレームが軽くなった分)。割り込み 1 回の実測は未 |
| 翻訳単位 | `FlexCAN_T4.h` を include するファイル 0。submodule を削除 |
| 初期化でのはまり | (Rx FIFO 版で) RFEN を立てた後にメールボックス 0〜15 を 0 で埋めると、FIFO エンジンの予約領域 (0x90〜0xDC) への書き込みで CPU が止まる (USB も tick も止まる)。クリアは RFEN の前に行う。メールボックス版では FIFO を使わない |
| RX を抜く | 抜いている間に TEC が上がり error passive (fault 1、TEC 128) まで進んだ。その間も Teensy は止まらず telemetry 49 Hz。挿し直すと 1 s 以内に active (TEC 0) に戻り送受信再開。バスオフまでは進まなかった (RX が浮いて dominant に見えると送信自体をしないため)。バスオフからの自動復帰は未確認 (CANH / CANL の短絡が要る) |
| バスオフ (2026-10-21) | モジュールをバスから外す → ACK が付かず TEC 128 で error passive に止まり (passive では ACK エラーで増えない)、送信の捨てが約 590/s。CANH–CANL を短絡 → dominant が recessive に見えてビットエラー → **bus-off**。短絡中は FlexCAN の自動復帰 (128 × 11 bit の recessive ≈ 1.4 ms) → 送り直し → active → passive → bus-off を**約 2 ms 周期で繰り返す** (1 kHz の SD ログ: bus-off 75 % / passive 24 % / active 1 %。bus-off 中の TEC は復帰の進み 0〜127 を示す)。短絡を外す → passive 128 で安定、バスに戻す → ACK で TEC が 1 ずつ減り 1 s 以内に active / TEC 0、送受信が再開。Teensy は全期間 telemetry 50 Hz のまま |

## 7. 決定事項

| 項目 | 決定 (2026-10-20) |
|---|---|
| 方針 | FlexCAN_T4 をやめて interface + 自前ドライバ。パッチ (static → inline) は採らない |
| 受信の文脈 | 割り込みの文脈でコールバック (今と同じ)。キュー / dispatch は作らない |
| 受信の形 | Rx FIFO ではなくメールボックス受信 (ID ごとに 1 本)。CAN FD では FIFO が使えないため (§4.8、段階 1 の決定) |
| CAN FD | 段階 1 (構造の準備: メールボックス受信、`CanFrame` 64 B、`CanConfig`、ストライド定数) まで入れる。有効化は FD の相手ができてから |
| 状態の見せ方 | telemetry の `CanStatus` (fault / TEC / REC / 累計 3 つ)。割り込み回数と synced は載せない (人には不要、fault と TEC で分かる) |
| 送信できないとき | 捨てて false (周期送信なので次で送る) |
| エラー割り込み | 使わない。状態は `state()` で読む。バスオフは自動復帰 |
| フィルタ | 受信メールボックスの ID と個別マスクで 0x740 / 0x741 だけ受ける (他の ID では割り込まない) |
| フレーム型 | `CanFrame { id, len, data[8] }` (kart-can の pack / unpack に合わせる) |
| ビットタイミング | 今の値 (12 Tq、サンプル点 75 %) を踏襲 |

## 8. 未決事項

なし。
