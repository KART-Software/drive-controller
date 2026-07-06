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

//////////////////////////////
/// Motor Control Settings ///
//////////////////////////////

#define MOTOR_CONTROLL_CYCLE_TIME 1  // ms

///////////////////////////////
/// Launch Control Settings ///
///////////////////////////////

// Launch FSM tick 周期。pulse counter (PULSE_UPDATE_INTERVAL_MS=100ms) と
// 整合させつつ FSM 応答性を確保するため 50ms (20Hz) とする。
// CAN_TX 周期 (16ms) からは切り離されている。
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

#define GPS_MAX_GEARS 7  // max(IST=5, Normal=7)

//////////////////////////
/// IMU Settings (SPI1) //
//////////////////////////

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
// Engine / クラッチ後(出力軸) RPM は QuadTimer ハードカウンタ (PulseCounter)。
//   Engine=14(QuadTimer3_2)  ClutchRPM=15(QuadTimer3_3)
#define PULSE_ENGINE_PIN 14
#define PULSE_CLUTCH_RPM_PIN 15
#define PULSE_UPDATE_INTERVAL_MS 100

////////////////////
/// CAN Settings ///
////////////////////

#define CAN_BITRATE 1000000       // 1 Mbps
#define CAN_TX_INTERVAL_MS 16     // ~60Hz
#define CAN_ID_GYRO_XY 0x600      // gx(float32) gy(float32)
#define CAN_ID_GYRO_Z_GEAR 0x601  // gz(float32) gear(8)
#define CAN_ID_ACCEL_XY 0x602     // ax(float32) ay(float32)
#define CAN_ID_ACCEL_Z 0x603      // az(float32)
// 制御信号 (data-logger → drive-controller)。制御用 CAN ID は 0x740 から始まり、
// 制御信号が増えれば 0x741, 0x742... を割り当てる。0x740 は:
//   byte0: ETC モード (CanEtcMode)
//   byte1: launch     (0x01=active / それ以外=inactive)
//   byte2: auto-shift  (0x01=ON(auto) / それ以外=OFF(manual))
#define CAN_ID_CONTROL 0x740

// 制御フレームが本値以上途絶したら各値を安全側へ戻す (フェールセーフ):
//   launch → false, mode → NORMAL(MOTOR_OFF はラッチ), auto-shift → OFF(manual)
#define CAN_CONTROL_TIMEOUT_MS 200

///////////////////////////////
/// Auto Shifter (GPIO)     ///
///////////////////////////////

// GPIO のみ (digitalRead/Write)。SPI1(0,1,26,27)・SPI0(10-13)・CAN3(30,31)・
// モーター(20-23)・車輪速(24,25,28,36)・RPM(14,15) と被らない空きピンを使う。
#define AUTO_SHIFT_UP_IN_PIN 40
#define AUTO_SHIFT_DOWN_IN_PIN 39
#define AUTO_SHIFT_UP_OUT_PIN 4
#define AUTO_SHIFT_DOWN_OUT_PIN 5
// 入力はプルアップ前提 (押下=LOW)。出力はアサート=HIGH。
#define AUTO_SHIFT_IN_ACTIVE LOW
#define AUTO_SHIFT_OUT_ACTIVE HIGH
#define AUTO_SHIFT_OUT_INACTIVE LOW

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
// 注意: ここで落とすのは点火系リレーであって、上の DC_MOTOR_RELAY_PIN(モーター電源)
//       とは別系統。MOTOR_OFF モード等では SHUTDOWN_RELAY は落とさない。
#define SHUTDOWN_RELAY_PIN 2
#define SHUTDOWN_SIG_IN_PIN 16
