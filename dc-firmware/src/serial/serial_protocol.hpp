#ifndef _SERIAL_PROTOCOL_H_
#define _SERIAL_PROTOCOL_H_

#include <Arduino.h>

#include "etc/error_handler.hpp"
#include "proto/drive_controller.pb.h"
#include "sensor/sensor_hub.hpp"

#define SERIAL_SPEED 115200
#define SENSOR_SEND_INTERVAL 20  // ms (50Hz)

// Maximum encoded payload size (proto bytes + 2 byte CRC). Keep this large
// enough for the biggest message we send (Config payload is the upper bound).
#define DC_MAX_PAYLOAD 768
// COBS overhead is at most ceil(N/254) plus the leading code byte.
#define DC_MAX_FRAME (DC_MAX_PAYLOAD + (DC_MAX_PAYLOAD / 254) + 2)

class SerialProtocol {
   public:
    static void initialize();

    // Sensor data (50 Hz).
    static void sendSensorData(const SensorHub& hub, bool isValid, const etc::ErrorHandler& errorHandler);

    // Debug log message.
    static void sendDebugf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
    static void sendDebugv(const char* fmt, va_list args);

    // Response without payload data.
    static void sendResponse(uint32_t id, bool ok);

    // Response with a typed oneof payload.
    static void sendResponseWithConfig(uint32_t id, bool ok, const dc_Config& cfg);
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

    // Drain Serial input, decoding any complete frames. If a Command was
    // decoded this call, copies it into `out` and returns true.
    static bool readCommand(dc_Command& out);
};

#endif
