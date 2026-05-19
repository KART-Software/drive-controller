#pragma once

#include <FlexCAN_T4.h>

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

// Data received over CAN
struct CanRxData {
    CanEtcMode etcMode = CanEtcMode::NORMAL;
    bool launchActive = false;
    unsigned long lastModeFrameMs = 0;    // MODE_SELECT を最後に受信した時刻 (millis)
    unsigned long lastLaunchFrameMs = 0;  // LAUNCH_CTRL を最後に受信した時刻 (millis)

    void mergeFrame(const CAN_message_t& msg);
    // 各フレームが一定時間途絶していたら安全側の値に戻す。
    //   - LAUNCH_CTRL 途絶 → launchActive = false
    //   - MODE_SELECT 途絶 → etcMode = NORMAL
    // CAN 断・ECU 故障時のフェールセーフ。
    void checkTimeouts(unsigned long nowMs);
};
