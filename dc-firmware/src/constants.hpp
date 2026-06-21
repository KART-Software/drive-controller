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

#define SENSOR_SAMPLING_RATE_US 125  // 8kHz sensor ISR interval

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
#define DC_MOTOR_SLP_PIN 4   // TODO: Teensy 4.1 のピン番号に変更
#define DC_MOTOR_PWM_PIN 16  // TODO: Teensy 4.1 の PWM 対応ピンに変更
#define DC_MOTOR_DIR_PIN 17  // TODO: Teensy 4.1 のピン番号に変更
#define DC_MOTOR_FLT_PIN 2   // TODO: Teensy 4.1 のピン番号に変更
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

#define PULSE_WHEEL_FL_PIN 5
#define PULSE_WHEEL_FR_PIN 6
#define PULSE_WHEEL_RL_PIN 7
#define PULSE_WHEEL_RR_PIN 8
#define PULSE_ENGINE_PIN 14
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
#define CAN_ID_MODE_SELECT 0x200
#define CAN_ID_LAUNCH_CTRL 0x300
#define CAN_ID_AUTO_SHIFT 0x400  // byte0: 0x01=ON(auto) / それ以外=OFF(manual)

// LAUNCH_CTRL フレームが本値以上途絶したら launchActive を false に落とす (フェールセーフ)
#define CAN_LAUNCH_TIMEOUT_MS 200
// MODE_SELECT フレームが本値以上途絶したら etcMode を NORMAL に戻す (フェールセーフ)
#define CAN_MODE_TIMEOUT_MS 200
// AUTO_SHIFT フレームが本値以上途絶したら OFF(manual) に戻す (フェールセーフ)
#define CAN_AUTO_SHIFT_TIMEOUT_MS 200

///////////////////////////////
/// Auto Shifter (GPIO)     ///
///////////////////////////////

// 注意: ピン 26/27 は SPI1 の MOSI/SCK (IMU 用)。ここに割り当てると autoShifter.begin()
// の pinMode で SPI1 が壊れ IMU read がハングする。SPI1(0,1,26,27) と被らない空きピンを使う。
#define AUTO_SHIFT_UP_IN_PIN 24     // TODO: 実配線に合わせる (ドライバー UP 入力)
#define AUTO_SHIFT_DOWN_IN_PIN 25   // TODO: 実配線に合わせる (ドライバー DOWN 入力)
#define AUTO_SHIFT_UP_OUT_PIN 28    // TODO: 実配線に合わせる (UP 出力 → IST コントローラ)
#define AUTO_SHIFT_DOWN_OUT_PIN 29  // TODO: 実配線に合わせる (DOWN 出力 → IST コントローラ)
// 入力はプルアップ前提 (押下=LOW)。出力はアサート=HIGH。
#define AUTO_SHIFT_IN_ACTIVE LOW
#define AUTO_SHIFT_OUT_ACTIVE HIGH
#define AUTO_SHIFT_OUT_INACTIVE LOW

/////////////////////////////////
/// Other Output Pin Settings ///
/////////////////////////////////

#define FUEL_PUMP_PIN 32       // TODO: Teensy 4.1 のピン番号に変更
#define DC_MOTOR_RELAY_PIN 33  // TODO: Teensy 4.1 のピン番号に変更
