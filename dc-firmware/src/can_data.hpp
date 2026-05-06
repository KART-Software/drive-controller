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

enum class EtcMode : uint8_t {
    CALIB = 1u,
    NORMAL = 2u,
    RESTRICTED = 3u,
    MOTOR_OFF = 4u,
};

// Data received over CAN
struct CanRxData {
    EtcMode etcMode = EtcMode::NORMAL;

    void mergeFrame(const CAN_message_t& msg);
};
