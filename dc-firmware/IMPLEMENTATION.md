# dc-firmware 拡張実装まとめ

## 概要

Teensy 4.1 (i.MX RT1062, ARM Cortex-M7 600MHz) 上の ETC (Electronic Throttle Control) ファームウェアに以下の機能を追加した。

1. **8kHz センサーサンプリング ISR** — 制御ループ (1kHz) の 8 倍速
2. **2 台目 ADS8688 ADC** — 同一 SPI バス、ソフトウェア CS
3. **SPI IMU (BMI270 / ICM-42688)** — SPI1 バス、コンパイル時選択
4. **5ch パルスカウンタ** — 車輪速 4ch + エンジン回転数
5. **CAN バス** — FlexCAN_T4、センサー TX + モード RX
6. **SelectSwitch → CAN 置換** — GPIO トグルスイッチを CAN モード受信に完全移行

---

## アーキテクチャ

```
┌─────────────────────────────────────────────────────┐
│  IntervalTimer (PIT)                                │
│                                                     │
│  motorControlISR @ 1kHz    NVIC priority 0 (最高)   │
│  sensorSamplingISR @ 8kHz  NVIC priority 16         │
│                                                     │
│         motor ISR は sensor ISR をプリエンプト可能   │
└─────────────────────────────────────────────────────┘

┌─ SPI (LPSPI4) ──────────────────┐
│  ADC1 (CS=10) ← ソフトウェア CS │  ADS8688 × 7ch (APPS, TPS, ITTR, BPS, MOTOR_I)
│  ADC2 (CS=9)  ← ソフトウェア CS │  ADS8688 × 0ch (拡張プレースホルダ)
│  Clock: 10MHz, Mode 1           │
└──────────────────────────────────┘

┌─ SPI1 (LPSPI3) ─────────────────┐
│  IMU (CS=0)  ← ハードウェア CS  │  BMI270 or ICM-42688
│  Clock: 10MHz/24MHz, Mode 3     │
└──────────────────────────────────┘

┌─ GPIO Interrupt (RISING) ───────┐
│  Wheel FL=5, FR=6, RL=7, RR=8  │  ~5kHz
│  Engine=14                      │  ~20kHz
└──────────────────────────────────┘

┌─ CAN1 (FlexCAN_T4) ────────────┐
│  TX=22, RX=23, 1Mbps            │
│  5 TX frames + 1 RX frame      │
└──────────────────────────────────┘
```

---

## Phase 1: 8kHz センサーサンプリング + ADC リファクタ + ADC2

### MovingAverage O(1) 最適化

**ファイル:** `moving_average.hpp` / `moving_average.cpp`

- `getAvg()` を O(n) ループ合算から **O(1) ランニングサム** に変更
- `add()` で `sum -= values[index]; values[index] = value; sum += value;`
- `getAvg()` は `return sum / size;` のみ
- 8kHz ISR 内で 60 サンプル (7.5ms ウィンドウ) を処理可能に

### ADC ドライバ汎用化

**ファイル:** `adc.hpp` / `adc.cpp`

| 変更点 | Before | After |
|--------|--------|-------|
| SPI クロック | 5MHz | **10MHz** (ADS8688 上限 17MHz) |
| CS 制御 | `portConfigRegister` ALT3 (HW CS) | **`digitalWriteFast` ソフトウェア CS** |
| チャンネル | ハードコード `#define` | **コンストラクタでチャンネル配列を受取** |
| コンストラクタ | `Adc()` | `Adc(csPin, channels, numChannels, spi)` |
| CS→読み取り遅延 | 2μs | **1μs** |

LPSPI4 は PCS0 (pin 10) しかヘッダ上に出ていないため、2 台の ADC に対してソフトウェア CS を採用。`digitalWriteFast` は 600MHz で約 1.67ns のため HW CS とほぼ等価。

### ADC2 追加

**ファイル:** `constants.hpp` / `globals.hpp` / `globals.cpp`

```cpp
// constants.hpp
#define ADC2_CS_PIN 9
#define ADC2_NUM_CH 0          // 拡張用プレースホルダ
#define ADC2_CHANNELS {0}

// globals.cpp
Adc gAdc(ADC_CS_PIN, adc1Channels, ADC_NUM_CH);    // 7ch
Adc gAdc2(ADC2_CS_PIN, adc2Channels, ADC2_NUM_CH); // 0ch (無効)
```

### 8kHz ISR タイマー

**ファイル:** `main.cpp`

```cpp
IntervalTimer sensorSamplingTimer;

void sensorSamplingISR() {
    gAdc.read();              // ADC1 全チャンネル読み取り
    apps1.read(); apps2.read(); ittr.read();
    tps1.read(); tps2.read(); bps.read();
    if (ADC2_NUM_CH > 0) gAdc2.read();
    imu.read();               // IMU バースト読み取り
}

// setup()
sensorSamplingTimer.begin(sensorSamplingISR, 125); // 125μs = 8kHz
motorControlTimer.priority(0);   // 最高優先度
sensorSamplingTimer.priority(16); // motor ISR がプリエンプト可能
```

**SPI バス時間バジェット (125μs):**
- ADC1 7ch: ~56μs (8μs/ch × 7) ← 10MHz SPI
- ADC2 0ch: 0μs
- IMU burst read: ~15μs
- 合計: ~71μs → **余裕 43%**

---

## Phase 2: IMU (BMI270 / ICM-42688)

### 抽象基底クラス

**ファイル:** `imu.hpp`

```cpp
class Imu {
public:
    virtual void begin() = 0;
    virtual void read() = 0;
    float accel[3] = {0};  // mg (milli-g)
    float gyro[3] = {0};   // dps (degrees per second)
};
```

### BMI270 ドライバ

**ファイル:** `bmi270.hpp` / `bmi270.cpp` / `bmi270_config.h`

- SPI1 (LPSPI3), Mode 3, 10MHz
- Accel: ±16g, ODR 1600Hz → 感度 0.488 mg/LSB
- Gyro: ±2000°/s, ODR 3200Hz → 感度 0.061 °/s/LSB
- 初期化: ソフトリセット → config file アップロード (Bosch 提供 ~8KB) → レジスタ設定
- `bmi270_config.h` はプレースホルダ（Bosch の実バイナリデータに差し替え必要）

### ICM-42688 ドライバ

**ファイル:** `icm42688.hpp` / `icm42688.cpp`

- SPI1 (LPSPI3), Mode 3, **24MHz**
- Accel: ±16g, ODR 8kHz → 感度 2048 LSB/g
- Gyro: ±2000°/s, ODR 8kHz → 感度 16.4 LSB/°/s
- ビッグエンディアンデータ形式 (BMI270 はリトルエンディアン)

### コンパイル時選択

**ファイル:** `constants.hpp`

```cpp
#define IMU_CS_PIN 0
// #define USE_BMI270
#define USE_ICM42688   // ← デフォルト
```

SPI モード衝突回避: ADC は Mode 1 (LPSPI4)、IMU は Mode 3 (LPSPI3) → 別バスのためモード切替不要。

---

## Phase 3: 5ch パルスカウンタ

**ファイル:** `pulse_counter.hpp` / `pulse_counter.cpp`

### 設計

- GPIO 割り込み (RISING) でカウント、`update()` で周波数算出
- 最大 5 インスタンス、**静的 ISR ディスパッチ**パターン

```cpp
static PulseCounter *instances[5];
static void isr0() { instances[0]->count++; }
//...
static constexpr IsrFunc isrTable[5] = {isr0, isr1, isr2, isr3, isr4};
```

### ピンアサイン

| チャンネル | ピン | 想定周波数 |
|-----------|------|-----------|
| Wheel FL  | 5    | ~5kHz     |
| Wheel FR  | 6    | ~5kHz     |
| Wheel RL  | 7    | ~5kHz     |
| Wheel RR  | 8    | ~5kHz     |
| Engine    | 14   | ~20kHz    |

### 周波数計算

```cpp
void PulseCounter::update() {
    uint32_t now = micros();
    uint32_t cnt = count;  // volatile 読み取り
    float elapsed = (now - lastUpdateUs) / 1e6f;
    if (elapsed > 0) frequencyHz = (cnt - lastCount) / elapsed;
    lastCount = cnt;
    lastUpdateUs = now;
}
```

`update()` は `loop()` 内で 100ms ごとに呼び出し。

---

## Phase 4: CAN バス

### CanBus クラス

**ファイル:** `can_bus.hpp` / `can_bus.cpp`

- FlexCAN_T4 ライブラリ (CAN1, FIFO モード)
- 1Mbps, TX=pin 22, RX=pin 23

### CAN メッセージ定義

#### TX フレーム (ECU → バス)

| ID | 名称 | 周期 | バイト配置 |
|----|------|------|-----------|
| `0x100` | Throttle | 20ms | APPS1(16) APPS2(16) TPS1(16) TPS2(16) — ×100 |
| `0x101` | Status | 20ms | target(16) BPS(16) errorFlags(16) mode(8) rsv(8) |
| `0x102` | WheelSpeed | 100ms | FL(16) FR(16) RL(16) RR(16) — Hz ×10 |
| `0x103` | Engine+IMU | 100ms | RPM(16) ax(16) ay(16) az(16) — accel ×1000 |
| `0x104` | Gyro | 100ms | gx(16) gy(16) gz(16) rsv(16) — dps ×10 |

全値 int16_t ビッグエンディアン。

#### RX フレーム (バス → ECU)

| ID | 名称 | データ |
|----|------|--------|
| `0x200` | ModeSelect | byte[0]: 0=Calib, 1=Normal, 2=Restrict |

### SelectSwitch → CAN 置換

**削除:** `#include "toggle_switch.hpp"`, `SelectSwitch3Pin selectSwitch`, `selectSwitch.initialize()`, `selectSwitch.read()`, `selectSwitch.changed()` ブロック

**追加:**
```cpp
// setup()
canBus.begin();
target.setModeNormal();  // CAN フレーム受信までデフォルト Normal

// loop()
uint8_t canMode;
if (canBus.poll(canMode)) {
    switch (canMode) {
    case 0: target.setModeCalibration(); break;
    case 1: target.setModeNormal(); break;
    case 2: target.setModeRestricted(); break;
    }
}
```

---

## テレメトリ拡張

**ファイル:** `serial_protocol.hpp` / `serial_protocol.cpp`

### 新規メッセージ: Extended Sensor Data (`"t": "x"`)

```cpp
static void sendExtendedData(float ax, float ay, float az,
                             float gx, float gy, float gz,
                             float wfl, float wfr, float wrl, float wrr,
                             float rpm);
```

JSON 出力例:
```json
{"t":"x","ts":12345,"ax":0.01,"ay":-0.02,"az":9.81,"gx":0.5,"gy":-0.3,"gz":0.1,"wfl":120.5,"wfr":121.0,"wrl":119.8,"wrr":120.2,"rpm":3500}
```

50Hz で既存の `"t":"s"` メッセージ直後に送信。

---

## ピンアサインまとめ

| ピン | 機能 | バス/方式 |
|------|------|----------|
| 10 | ADC1 CS | SPI (LPSPI4) SW CS |
| 9 | ADC2 CS | SPI (LPSPI4) SW CS |
| 11 | SPI MOSI | LPSPI4 |
| 12 | SPI MISO | LPSPI4 |
| 13 | SPI SCK | LPSPI4 |
| 0 | IMU CS | SPI1 (LPSPI3) HW CS |
| 1 | SPI1 MISO | LPSPI3 |
| 26 | SPI1 MOSI | LPSPI3 |
| 27 | SPI1 SCK | LPSPI3 |
| 5 | Pulse Wheel FL | GPIO Interrupt |
| 6 | Pulse Wheel FR | GPIO Interrupt |
| 7 | Pulse Wheel RL | GPIO Interrupt |
| 8 | Pulse Wheel RR | GPIO Interrupt |
| 14 | Pulse Engine | GPIO Interrupt |
| 22 | CAN TX | CAN1 |
| 23 | CAN RX | CAN1 |
| 16 | Motor PWM | PWM |
| 17 | Motor DIR | GPIO |
| 4 | Motor SLP | GPIO |
| 2 | Motor FLT | GPIO |
| 32 | Fuel Pump | GPIO |
| 33 | Motor Relay | GPIO |

**解放されたピン:** 24 (BUTTON_1), 25 (BUTTON_2), 26 → SPI1 MOSI に転用

---

## ビルド情報

```
Platform: teensy (PlatformIO)
Board: teensy41
Framework: arduino
Libraries: ArduinoJson ^6.21.3, FlexCAN_T4 (GitHub)

FLASH: code 128,472 + data 10,804 = 139,276 bytes (1.7%)
RAM1:  variables 22,848 + code 123,416 = 146,264 bytes (28%)
RAM2:  variables 12,416 bytes (2.4%)
```

---

## 変更ファイル一覧

### 新規作成
| ファイル | 内容 |
|---------|------|
| `imu.hpp` | IMU 抽象基底クラス |
| `bmi270.hpp` / `bmi270.cpp` | BMI270 SPI ドライバ |
| `bmi270_config.h` | BMI270 設定ファイルプレースホルダ |
| `icm42688.hpp` / `icm42688.cpp` | ICM-42688 SPI ドライバ |
| `pulse_counter.hpp` / `pulse_counter.cpp` | 5ch パルスカウンタ |
| `can_bus.hpp` / `can_bus.cpp` | CAN バスドライバ |

### 変更
| ファイル | 内容 |
|---------|------|
| `moving_average.hpp` / `moving_average.cpp` | O(1) ランニングサム |
| `adc.hpp` / `adc.cpp` | ソフトウェア CS, 10MHz, チャンネル配列コンストラクタ |
| `constants.hpp` | ADC2, IMU, パルスカウンタ, CAN 設定追加 |
| `globals.hpp` / `globals.cpp` | gAdc2 追加 |
| `main.cpp` | 8kHz ISR, IMU, パルスカウンタ, CAN 統合, SelectSwitch 削除 |
| `serial_protocol.hpp` / `serial_protocol.cpp` | `sendExtendedData()` 追加 |
| `platformio.ini` | FlexCAN_T4 ライブラリ追加 |

---

## 残課題

- [ ] `bmi270_config.h` に Bosch の実バイナリ設定データを配置
- [ ] ADC2 にセンサー割り当て時に `ADC2_NUM_CH` / `ADC2_CHANNELS` を設定
- [ ] etc-console の UI に IMU / パルスカウンタ / CAN データ表示を追加
- [ ] CAN DBC ファイル作成（他 ECU との連携用）
- [ ] ピン番号の最終確認 (TODO コメント付きのピンがまだ残存)
