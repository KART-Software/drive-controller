#pragma once

#include <FlexCAN_T4.h>

#include "util/toggle_switch.hpp"

// Data to transmit over CAN at each 60Hz tick
struct CanTxData {
    int8_t gear;
    float gyro[3];   // gx, gy, gz (dps)
    float accel[3];  // ax, ay, az (m/s²)

    // 0x600: gx(float32) gy(float32)
    // 0x601: gz(float32) gear(8)
    // 0x602: ax(float32) ay(float32)
    // 0x603: az(float32)
    static constexpr uint8_t FRAME_COUNT = 4;
    void toFrames(CAN_message_t (&out)[FRAME_COUNT]) const;
};

// CAN 受信フレーム由来の ETC モード (proto の dc_EtcMode とは独立)
enum class CanEtcMode : uint8_t {
    CALIB = 1u,
    NORMAL = 2u,
    RESTRICTED = 3u,
    MOTOR_OFF = 4u,
};

// GPIO 3ピンセレクタ位置 → ETC モード (GPIO 制御入力用。main.cpp / CanController で共有)。
//   未選択(Zero)=CALIB / First=NORMAL / Second=RESTRICTED / Third=MOTOR_OFF
CanEtcMode selectToEtcMode(SelectSwitch3Pin::Status s);

// Data received over CAN
struct CanRxData {
    CanEtcMode etcMode = CanEtcMode::NORMAL;
    bool launchActive = false;
    bool autoShiftActive = false;          // auto-shift: true=ON(auto) / false=OFF(manual)
    unsigned long lastControlFrameMs = 0;  // 制御フレーム(0x740)を最後に受信した時刻 (millis)

    void mergeFrame(const CAN_message_t& msg);
    // 制御フレームが一定時間途絶していたら安全側の値に戻す。
    //   - launch     途絶 → launchActive = false
    //   - mode       途絶 → etcMode = NORMAL (MOTOR_OFF はラッチ)
    //   - auto-shift 途絶 → autoShiftActive = false (OFF/manual)
    // CAN 断・ECU 故障時のフェールセーフ。
    void checkTimeouts(unsigned long nowMs);
};
