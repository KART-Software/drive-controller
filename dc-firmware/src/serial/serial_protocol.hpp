#pragma once

#include <Arduino.h>

#include <optional>

#include "etc/error_handler.hpp"
#include "proto/drive_controller.pb.h"
#include "sensor/sensor_hub.hpp"
#include "control/control_input.hpp"
#include "util/loop_stats.hpp"

#define SENSOR_SEND_INTERVAL 20  // ms (50Hz)

// Maximum encoded payload size (proto bytes + 2 byte CRC). Keep this large
// enough for the biggest message we send (Config payload is the upper bound).
#define DC_MAX_PAYLOAD 768
// COBS overhead is at most ceil(N/254) plus the leading code byte.
#define DC_MAX_FRAME (DC_MAX_PAYLOAD + (DC_MAX_PAYLOAD / 254) + 2)

// ETC モードの proto ↔ ドメイン変換 (CAN ↔ ドメインは can_data.cpp)
dc_EtcMode etcModeToProto(EtcTarget::Mode m);
EtcTarget::Mode etcModeFromProto(dc_EtcMode m);  // 未指定・未知値は MotorOff (安全側)

// USB シリアルは Teensy コアが起動時 (startup の usb_init) に用意するので初期化は要らない。
// Serial.begin() は呼ばないこと: Teensy 4 の begin() はホストがポートを開くまで最大 2 s (USB 未接続でも 0.75 s)
// 待つ (cores/teensy4/usb_serial.h)。送信は空きを見て捨てるので、つながっていなくても固まらない。
// 起動時の debug は SerialDebugWriter がつながるまでためる。
class SerialProtocol {
   public:
    // Sensor data (50 Hz).
    static void sendSensorData(const SensorHub& hub,
                               bool isValid,
                               const etc::ErrorHandler& errorHandler,
                               float duty,
                               const LoopStats::Snapshot& sys,
                               const ControlInput& control,
                               uint32_t nowMs);  // telemetry の timestamp (loop の時刻)

    // Debug log message. 直接呼ばず DebugLogger::log を使う (SerialDebugWriter がこれを呼ぶ)
    static void sendDebugv(uint64_t timestampUs, const char* fmt, va_list args);

    // Response without payload data.
    static void sendResponse(uint32_t id, bool ok);

    // Response with a typed oneof payload.
    static void sendResponseWithConfig(uint32_t id, bool ok, const dc_ConfigResponse& cfg);
    static void sendResponseWithAppsMin(uint32_t id, bool ok, const dc_AppsMinResponse& p);
    static void sendResponseWithAppsMax(uint32_t id, bool ok, const dc_AppsMaxResponse& p);
    static void sendResponseWithTpsMin(uint32_t id, bool ok, const dc_TpsMinResponse& p);
    static void sendResponseWithTpsMax(uint32_t id, bool ok, const dc_TpsMaxResponse& p);
    static void sendResponseWithIdling(uint32_t id, bool ok, const dc_IdlingResponse& p);
    static void sendResponseWithTargetBound(uint32_t id, bool ok, const dc_TargetBoundResponse& p);
    static void sendResponseWithIttr(uint32_t id, bool ok, const dc_IttrResponse& p);
    static void sendResponseWithFlags(uint32_t id, bool ok, const dc_EtcPlausibilityCheckFlags& p);
    static void sendResponseWithPid(uint32_t id, bool ok, const dc_EtcPid& p);
    static void sendResponseWithCurve(uint32_t id, bool ok, const dc_EtcTargetCurve& p);
    static void sendResponseWithGpsGear(uint32_t id, bool ok, const dc_GpsGearResponse& p);

    // Drain Serial input, decoding any complete frames. If a Command was
    // decoded this call, copies it into `out` and returns true.
    static std::optional<dc_Command> readCommand();  // 1 フレーム揃えば返す (無ければ nullopt)
};
