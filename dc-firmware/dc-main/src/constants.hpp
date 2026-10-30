#pragma once

////////////////////
/// ADC Settings ///
////////////////////

#define ADC_CS_PIN 10
#define APPS_1_CH 0
#define APPS_2_CH 1
#define TPS_1_CH 7
#define TPS_2_CH 6
#define ITTR_CH 2           // IST Throttle Position Target Receiver (ITTR)
#define BPS_CH 3            // Brake Pressure Sensor
#define MOTOR_CURRENT_CH 4  // Connected to motor driver's CS PIN.The voltage is about 20 mV/A plus a 50 mV offset.
// TODO[bench]: 2 台目 ADS8688 接続時に GPS_CH=8 / CLUTCH_CH=9 (2nd dev) へ戻す。
// 現在は 1 台 (_adc<1>, ch0-7) のみのため範囲外参照を避けて空き ch5 に退避。
#define GPS_CH 5            // Gear Position Sensor (本来 2nd ADS8688 CH0 = 8)
#define CLUTCH_CH 5         // Clutch Position Sensor (本来 2nd ADS8688 CH1 = 9)

/////////////////////////
/// Sampling Settings ///
/////////////////////////

#define SENSOR_SAMPLING_RATE_US 62.5  // 16kHz sensor ISR interval (IntervalTimer は float us 可)
// IMU はサンプリング ISR の何回に 1 回読むか (DMA 経路)。16 kHz / 2 = 8 kHz で、IAM-20680HP のジャイロの内部更新 (8 kHz) に合わせる。
// 1 回の転送 (15 バイト @ 8 MHz、約 15 µs) が次に読むまで (SENSOR_SAMPLING_RATE_US × これ) に終わること。
// 変えたら IAM20680_AVG_WINDOW (1 ms 分) も合わせる (sensor_hub.cpp の static_assert で確かめる)
#define IMU_SAMPLING_DIVIDER 2

////////////////////
/// PWM Settings ///
////////////////////

// #define HILITAND
// #define VNH5019
#define G2_18V17
// #define DRV8256P
// #define TB67H450

#ifdef HILITAND
#define DC_MOTOR_ENABLE_PIN 5  // TODO: Teensy 4.1 のピン番号に変更
#define DC_MOTOR_PWM_1_PIN 33  // TODO: Teensy 4.1 の PWM 対応ピンに変更
#define DC_MOTOR_PWM_2_PIN 32  // TODO: Teensy 4.1 の PWM 対応ピンに変更
#define MOTOR_DIRECTION 1
#endif

#ifdef VNH5019
#define DC_MOTOR_IN_A_PIN 17  // TODO: Teensy 4.1 のピン番号に変更
#define DC_MOTOR_IN_B_PIN 2   // TODO: Teensy 4.1 のピン番号に変更
#define DC_MOTOR_EN_A_PIN 16  // TODO: Teensy 4.1 のピン番号に変更
#define DC_MOTOR_EN_B_PIN 0   // TODO: Teensy 4.1 のピン番号に変更
#define DC_MOTOR_PWM_PIN 4    // TODO: Teensy 4.1 の PWM 対応ピンに変更
#define MOTOR_DIRECTION 1
#endif

#ifdef G2_18V17
#define DC_MOTOR_SLP_PIN 21
#define DC_MOTOR_PWM_PIN 22  // FlexPWM4_0 (PWM 出力)
#define DC_MOTOR_DIR_PIN 23
#define DC_MOTOR_FLT_PIN 20
#define MOTOR_DIRECTION -1
#endif

#ifdef DRV8256P
#define DC_MOTOR_PWM_1_PIN 17  // TODO: Teensy 4.1 の PWM 対応ピンに変更
#define DC_MOTOR_PWM_2_PIN 16  // TODO: Teensy 4.1 の PWM 対応ピンに変更
#define DC_MOTOR_FLT_PIN 4     // TODO: Teensy 4.1 のピン番号に変更
#define DC_MOTOR_SLP_PIN 2     // TODO: Teensy 4.1 のピン番号に変更
#define MOTOR_DIRECTION 1
#endif

#ifdef TB67H450
#define DC_MOTOR_IN_1_PIN 16  // TODO: Teensy 4.1 の PWM 対応ピンに変更
#define DC_MOTOR_IN_2_PIN 17  // TODO: Teensy 4.1 の PWM 対応ピンに変更
#define MOTOR_DIRECTION 1
#endif

///////////////////////////////////////////////
/// 割り込み (docs/loop_nonblocking_spec.md §3.1) ///
///////////////////////////////////////////////

// NVIC 優先度 (上位 4 bit のみ有効、小さいほど優先)。tickISR と canRxISR は同じ優先度にして互いに割り込ませない
// (シフター・CanRxData・ControlInput を排他なしで共有できる)
#define TICK_ISR_PRIORITY 64
#define CAN_RX_ISR_PRIORITY 64
#define SENSOR_SAMPLING_ISR_PRIORITY 208  // USB (128) より下
#define TICK_PERIOD_US 1000

///////////////////////////////
/// Launch Control Settings ///
///////////////////////////////

// Launch FSM tick 周期 (50ms, 20Hz)。凍結中は loop に残している。凍結解除時に tickISR へ移す
// (docs/loop_nonblocking_spec.md §6.7)。
#define LAUNCH_UPDATE_INTERVAL_MS 50

///////////////////////
/// Sensor Settings ///
///////////////////////

#define TPS_MIN 0
#define TPS_MAX 100
#define TPS_MARGIN 15
#define TPS_LARGE_OPEN_THRESHOLD 50

#define APPS_MIN 0
#define APPS_MAX 100
#define APPS_MARGIN 20

#define ITTR_MARGIN 0

#define BPS_RAW_MAX (4.5 * 65535 / 5.12)  // 4.5V
#define BPS_RAW_MIN 5000                  // 0.5V

#define BPS_MAX 1000                     // psi
#define BPS_MIN 0                        // psi
#define BPS_HIGH_PRESSURE_THRESHOLD 600  // psi
#define BPS_MARGIN 50                    // psi

///////////////////////////
/// Clutch Sensor (CLS) ///
///////////////////////////

#define CLUTCH_MIN 0
#define CLUTCH_MAX 100
#define CLUTCH_MARGIN 5

///////////////////////////////////
/// Gear Position Sensor (GPS)  ///
///////////////////////////////////

#define GPS_TOLERANCE 2000  // raw 値の判定許容誤差

// IST transmission: N, 1, 2, 3, 4 (5 positions)
#define GPS_IST_GEAR_COUNT 5
#define GPS_IST_GEARS \
    { 0, 1, 2, 3, 4 }

// Normal transmission: 1, N, 2, 3, 4, 5, 6 (7 positions)
#define GPS_NORMAL_GEAR_COUNT 7
#define GPS_NORMAL_GEARS \
    { 1, 0, 2, 3, 4, 5, 6 }

#define GPS_MAX_GEARS 7

//////////////////////////
/// IMU Settings (SPI1) //
//////////////////////////

// SPI1 (LPSPI3) は IMU 専用: DMA 経路では beginDma() 以降トランザクションを開いたままにし、CS (pin 0) を
// ハードウェア (LPSPI3 PCS0) に切り替えるので、SPI1 にほかのデバイスはつなげない
#define IMU_CS_PIN 0

/////////////////////////////
/// Pulse Counter Settings //
/////////////////////////////

// 車輪速は FlexPWM 入力キャプチャ (FreqMeasureMulti) で計測。各輪は別 FlexPWM
// サブモジュールに割り当てること (同一サブモジュールは同時計測不可):
//   FL=24(PWM1_2)  FR=25(PWM1_3)  RL=28(PWM3_1)  RR=36(PWM2_3)
#define PULSE_WHEEL_FL_PIN 24
#define PULSE_WHEEL_FR_PIN 25
#define PULSE_WHEEL_RL_PIN 28
#define PULSE_WHEEL_RR_PIN 36
// Engine / クラッチ後(出力軸) RPM は QuadTimer3 (QuadTimerEdgeSource: ch2/ch3 で数え、ch0/ch1 でエッジ時刻をキャプチャ)。
//   Engine=14(QuadTimer3_2)  ClutchRPM=15(QuadTimer3_3)
#define PULSE_ENGINE_PIN 14
#define PULSE_CLUTCH_RPM_PIN 15

////////////////////
/// CAN Settings ///
////////////////////

#define CAN3_RX_PIN 30  // CRX3 (GPIO_EMC_37)。Flexcan3Bus が設定するパッドと一致すること (static_assert)
#define CAN3_TX_PIN 31  // CTX3 (GPIO_EMC_36)
#define CAN_BITRATE 1000000       // 1 Mbps。kart-can can.yaml の baudrate と同じ値にする (生成ヘッダにはマクロが出ない)
// CAN ID / メッセージ定義と送信周期は kart-can (submodule dc-firmware/dc-main/lib/kart-can、生成 kart_can.h) が単一ソース。
// ここでは定義しない。途絶時の扱いは ControlInput (kart-can docs/can-spec.md §4.1)
#define CAN_CONTROL_TIMEOUT_MS 200
#define CAN_SHIFT_TIMEOUT_MS 100

///////////////////////////////
/// Auto Shifter (GPIO)     ///
///////////////////////////////

// 出力 GPIO。SPI1(0,1,26,27)・SPI0(10-13)・CAN3(30,31)・
// モーター(20-23)・車輪速(24,25,28,36)・RPM(14,15) と被らない空きピンを使う。
#define AUTO_SHIFT_UP_OUT_PIN 4
#define AUTO_SHIFT_DOWN_OUT_PIN 5
#define AUTO_SHIFT_OUT_ACTIVE HIGH
#define AUTO_SHIFT_OUT_INACTIVE LOW
// パルスの開始から次の開始までの最短 (ms)。config の cooldown_ms がこれより短ければこちらを使う
#define SHIFT_MIN_INTERVAL_MS 500

//////////////////////////////////////
/// Starter (セルモーターリレー) ///
//////////////////////////////////////

// セルモーターリレーの出力 (docs/starter_spec.md)。HIGH = ON。暫定の空きピン (配線確定後に見直す)
#define STARTER_RELAY_PIN 6

//////////////////////////////////////
/// ETC Experiment Mode (同定実験) ///
//////////////////////////////////////

// docs/etc_experiment_mode_spec.md。シーケンス定義は experiment_runner.cpp。
// ガード帯は拡張ストッパー (-15/115%) の内側かつ TPS_MARGIN(15) の内側 =
// プラウシビリティ回路チェックより必ず先に効く。
#define EXPERIMENT_TP_GUARD_LOW (-10.0)  // リリース試験中は不適用
#define EXPERIMENT_TP_GUARD_HIGH (110.0)
// 静止判定 (全実験共通): 窓内で TPS と duty の変動幅が閾値未満なら静止。
// duty も見る理由: 摩擦帯で θ 張り付き中は I 項がランプしている (真の平衡でない)。
#define EXPERIMENT_STATIONARY_TP_TOL (0.2)
#define EXPERIMENT_STATIONARY_DUTY_TOL (0.5)
#define EXPERIMENT_STATIONARY_WINDOW_MS 500
#define EXPERIMENT_SETTLE_TIMEOUT_MS 8000  // 静止しなくても黙って次へ進む (件数だけ数える)
#define EXPERIMENT_RECORD_HOLD_MS 1000     // 静止してからの記録窓
#define EXPERIMENT_RELEASE_HOLD_MS 1000
// ストール (PID がほぼ飽和 + 目標乖離 + 不動が続く) で中断。仮値、実測後に調整
#define EXPERIMENT_STALL_DUTY (90.0)
#define EXPERIMENT_STALL_TARGET_DEV (2.0)
#define EXPERIMENT_STALL_MS 1000

/////////////////////////////////
/// Other Output Pin Settings ///
/////////////////////////////////

#define DC_MOTOR_RELAY_PIN 3  // モーター電源リレー (DcMotor が制御。SHUTDOWN 回路とは別物)

// ── SHUTDOWN 回路 (点火系の AND 安全回路) ──────────────────────────────
// 車両の燃料/点火系は、複数のリレー/スイッチの AND (= SHUTDOWN 回路) が閉じている
// ときのみ起動する。drive-controller はその AND の 1 要素となるリレー
// (SHUTDOWN_RELAY_PIN) を制御し、AND 全体の結果を SHUTDOWN_SIG_IN_PIN で受け取る。
//   - SHUTDOWN_RELAY_PIN: OUTPUT。HIGH=リレーClose=点火系許可。通常 HIGH。
//       プラウシビリティ違反時のみ LOW に落とす (= 点火系遮断、復帰不可ラッチ)。
//   - SHUTDOWN_SIG_IN_PIN: INPUT (外部で PULLDOWN 済みなので素の INPUT)。
//       AND 回路の出力。HIGH=ETC 動作許可 / LOW=ETC 停止。ToggleSwitch でデバウンス。
#define SHUTDOWN_RELAY_PIN 2
#define SHUTDOWN_SIG_IN_PIN 16
