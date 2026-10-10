#pragma once

#include <FlexCAN_T4.h>

#include <array>
#include <optional>

#include "sensor/sensors.hpp"

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
    std::array<CAN_message_t, FRAME_COUNT> toFrames() const;
};

// このノードの状態 (0x60A DC_Status, 33 ms)。0x740 送信の後継 (kart-can 307e038)。
// ドメインの型で受け取り、CAN の表現への変換は toFrame() の中で行う
struct CanStatusData {
    EtcTarget::Mode etcMode;  // 適用中の ETC モード (EtcTarget)
    bool launchActive;        // 凍結中 (常に false)
    bool autoShift;           // 適用中のオートシフト ON/OFF
    bool starterRelay;        // セルモーターリレー出力 (未実装、常に false)
    bool shutdownLoopClosed;  // Shutdown 回路 (AND) の出力 = SIG_IN
    CAN_message_t toFrame() const;
};

// 受信データ: 0x740 Control と 0x741 Shift (送り手は data_logger)。
// 最後に受信したフレームの値と受信時刻だけを持つ。途絶時のフォールバックや未受信時の扱いなど
// 「どう使うか」は ControlInput が決める (kart-can docs/can-spec.md §4.1)。
struct CanRxData {
    // 0x740 Control
    std::optional<EtcTarget::Mode> etcMode;  // 最後のフレームの値。UNSPECIFIED (0) や未知値は nullopt
    bool launchActive = false;
    bool autoShiftActive = false;  // true=ON(auto) / false=OFF(manual)
    bool starter = false;          // セルスイッチ (受信のみ。セルモーター制御は別仕様)
    uint32_t controlFrames = 0;    // 受信数 (新しいフレームの検出用。0 = 未受信)
    unsigned long lastControlFrameMs = 0;
    // 0x741 Shift (押下状態。立ち上がり検出は AutoShifter が行う)
    bool shiftUp = false;
    bool shiftDown = false;
    uint32_t shiftFrames = 0;
    unsigned long lastShiftFrameMs = 0;

    void mergeFrame(const CAN_message_t& msg);
};
