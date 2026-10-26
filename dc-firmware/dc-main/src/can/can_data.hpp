#pragma once

#include <array>
#include <optional>

#include "interfaces/can/can_frame.hpp"
#include "sensor/sensors.hpp"

struct CanImuData {
    int8_t gear;
    float gyro[3];   // [dps]
    float accel[3];  // [mg]

    static constexpr uint8_t FRAME_COUNT = 4;
    std::array<CanFrame, FRAME_COUNT> toFrames() const;
};

// センサーの物理値と生値 (0x604 DC_Pedals / 0x605 DC_EtcRaw / 0x607 DC_WheelSpeed / 0x608 DC_WheelPulse / 0x609 DC_Engine、16 ms)。
// 0x606 DC_StrokeRear (後ろサスのストローク) は 2 台目の ADC が未接続なので送らない。
// 値はドメインの単位で受け取り、kart-can のスケール (uint16) への変換は toFrames() の中で行う
struct CanSensorData {
    float appsPercent;  // ETC が使う APPS1
    float tpsPercent;   // ETC が使う TPS1
    float clutchPercent;
    float brakeRearPsi;
    uint16_t appsRaw[2];  // ADS8688 のカウント (0〜65535 = 0〜5.12 V)
    uint16_t tpsRaw[2];
    float wheelKmh[4];        // FL, FR, RL, RR
    uint32_t wheelPulses[4];  // FL, FR, RL, RR
    float engineRpm;
    float clutchRpm;  // クラッチ後 (出力軸)

    static constexpr uint8_t FRAME_COUNT = 5;
    std::array<CanFrame, FRAME_COUNT> toFrames() const;
};

// このノードの状態 (0x60A DC_Status, 33 ms)。ドメインの型で受け取り、CAN の表現への変換は toFrame() の中で行う
struct CanStatusData {
    EtcTarget::Mode etcMode;
    bool launchActive;  // Launch Control は凍結中なので常に false
    bool autoShift;
    bool starterRelay;
    bool shutdownLoopClosed;  // Shutdown 回路 (AND) の出力 = SIG_IN
    CanFrame toFrame() const;
};

// 受信データ: 0x740 Control と 0x741 Shift (送り手は data_logger)。
// 最後に受信したフレームの値と受信時刻だけを持つ。途絶時のフォールバックや未受信時の扱いなど
// 「どう使うか」は ControlInput が決める (kart-can docs/can-spec.md §4.1)。
struct CanRxData {
    // 0x740 Control
    std::optional<EtcTarget::Mode> etcMode;  // 最後のフレームの値。UNSPECIFIED (0) や未知値は nullopt
    bool launchActive = false;
    bool autoShiftActive = false;
    bool starter = false;
    uint32_t controlFrames = 0;    // 受信数 (新しいフレームの検出用。0 = 未受信)
    unsigned long lastControlFrameMs = 0;
    // 0x741 Shift (押下状態。立ち上がり検出は AutoShifter が行う)
    bool shiftUp = false;
    bool shiftDown = false;
    uint32_t shiftFrames = 0;
    unsigned long lastShiftFrameMs = 0;

    void mergeFrame(const CanFrame& frame, uint32_t nowMs);  // 受信割り込みから。nowMs を受信時刻として刻む
};

// 受信割り込みの中ですぐ処理すべきフレームか (シフトパドル 0x741。tick を待つと最大 1 ms 遅れる)。
// それ以外 (0x740 など) は次の tickISR で反映すれば足りる
bool needsImmediateHandling(const CanFrame& frame);
