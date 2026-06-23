#include "serial_protocol.hpp"

#include <pb_decode.h>
#include <pb_encode.h>
#include <stdarg.h>
#include <stdio.h>

#include "cobs.hpp"
#include "crc16.hpp"
namespace {

uint8_t rxBuf[DC_MAX_FRAME];
size_t rxLen = 0;

dc_EtcMode targetModeToProto(const EtcTarget& t) {
    switch (t.getMode()) {
        case EtcTarget::Mode::Calibration:
            return dc_EtcMode_ETC_MODE_CALIB;
        case EtcTarget::Mode::Normal:
            return dc_EtcMode_ETC_MODE_NORMAL;
        case EtcTarget::Mode::Restricted:
            return dc_EtcMode_ETC_MODE_RESTRICT;
        case EtcTarget::Mode::MotorOff:
            return dc_EtcMode_ETC_MODE_MOTOR_OFF;
    }
    return dc_EtcMode_ETC_MODE_UNSPECIFIED;
}

// ---------------------------------------------------------------------------
// Fused CRC16 + COBS encoder.
// Streams protobuf bytes directly into a COBS-framed output buffer while
// computing CRC16, eliminating the intermediate plain-protobuf buffer.
// ---------------------------------------------------------------------------
struct CobsCrcWriter {
    uint8_t* buf;
    size_t capacity;
    size_t writeIdx;
    size_t codeIdx;
    uint8_t code;
    uint16_t crc;
    bool overflow;

    void init(uint8_t* dst, size_t cap) {
        buf = dst;
        capacity = cap;
        writeIdx = 1;  // reserve first code byte
        codeIdx = 0;
        code = 1;
        crc = 0xFFFF;
        overflow = false;
    }

    // Feed one payload byte: updates CRC and COBS-encodes.
    inline void feedByte(uint8_t b) {
        crc = crc16_update(crc, b);
        cobsByte(b);
    }

    // COBS-encode one byte (no CRC update).
    inline void cobsByte(uint8_t b) {
        if (writeIdx >= capacity) {
            overflow = true;
            return;
        }
        if (b == 0x00) {
            buf[codeIdx] = code;
            codeIdx = writeIdx++;
            code = 1;
        } else {
            buf[writeIdx++] = b;
            code++;
            if (code == 0xFF) {
                buf[codeIdx] = code;
                codeIdx = writeIdx++;
                code = 1;
            }
        }
    }

    // Append CRC (LE) via COBS, close the COBS block, write 0x00 delimiter.
    // Returns total frame length including delimiter, or 0 on overflow.
    size_t finalize() {
        cobsByte((uint8_t)(crc & 0xFF));
        cobsByte((uint8_t)((crc >> 8) & 0xFF));
        if (writeIdx < capacity)
            buf[codeIdx] = code;  // close last COBS block
        if (writeIdx < capacity)
            buf[writeIdx++] = 0x00;  // delimiter
        return overflow ? 0 : writeIdx;
    }
};

// nanopb output-stream callback: feeds bytes into CobsCrcWriter.
static bool cobsCrcWriteCb(pb_ostream_t* stream, const pb_byte_t* buf, size_t count) {
    auto* w = static_cast<CobsCrcWriter*>(stream->state);
    for (size_t i = 0; i < count; i++) {
        w->feedByte(buf[i]);
    }
    return !w->overflow;
}

// droppable=true のフレーム(=高頻度テレメトリ)は、USB TX バッファに空きが無ければ
// 送らずに捨てる。これがないと Serial.write がバッファ満杯でブロックし、loop() が止まって
// コマンド受信/応答(commandRouter.poll)が滞り、ホスト側が 3s タイムアウトする。
bool encodeAndWrite(const pb_msgdesc_t* fields, const void* src_struct, bool droppable = false) {
    uint8_t frame[DC_MAX_FRAME];
    CobsCrcWriter writer;
    writer.init(frame, sizeof(frame));

    pb_ostream_t stream = {cobsCrcWriteCb, &writer, DC_MAX_PAYLOAD, 0};
    if (!pb_encode(&stream, fields, src_struct))
        return false;

    size_t len = writer.finalize();
    if (len == 0)
        return false;

    if (droppable && (size_t)Serial.availableForWrite() < len)
        return false;  // ホストの取り込みが追いつかない: テレメトリをドロップ (loop を止めない)

    Serial.write(frame, len);
    return true;
}

void sendDeviceMessage(const dc_DeviceToHost& msg, bool droppable = false) {
    encodeAndWrite(dc_DeviceToHost_fields, &msg, droppable);
}

void sendResponseInternal(const dc_Response& resp) {
    dc_DeviceToHost env = dc_DeviceToHost_init_zero;
    env.which_payload = dc_DeviceToHost_response_tag;
    env.payload.response = resp;
    sendDeviceMessage(env);
}

}  // namespace

void SerialProtocol::initialize() {
    Serial.begin(SERIAL_SPEED);
    while (!Serial && millis() < 3000) {
        // USB Serial 接続待ち（最大3秒）
    }
}

void SerialProtocol::sendSensorData(const SensorHub& hub, bool isValid, const etc::ErrorHandler& errorHandler) {
    dc_DeviceToHost env = dc_DeviceToHost_init_zero;
    env.which_payload = dc_DeviceToHost_sensor_tag;
    dc_State& st = env.payload.sensor;

    st.timestamp = millis();

    st.has_sensor = true;
    dc_Sensor& s = st.sensor;
    s.sps = hub.adc().sps();
    s.apps1_raw = hub.apps1().getRawValue();
    s.apps2_raw = hub.apps2().getRawValue();
    s.ittr_raw = hub.ittr().getRawValue();
    s.tps1_raw = hub.tps1().getRawValue();
    s.tps2_raw = hub.tps2().getRawValue();
    s.bps_raw = hub.bps().getRawValue();

    s.apps1 = (float)hub.apps1().convertedValue();
    s.apps2 = (float)hub.apps2().convertedValue();
    s.ittr = (float)hub.ittr().convertedValue();
    s.tps1 = (float)hub.tps1().convertedValue();
    s.tps2 = (float)hub.tps2().convertedValue();
    s.bps = (float)hub.bps().convertedValue();

    s.target_tp = (float)hub.target().getTarget();

    if (hub.imu() != nullptr) {
        s.accel_x = hub.imu()->accel[0];
        s.accel_y = hub.imu()->accel[1];
        s.accel_z = hub.imu()->accel[2];
        s.gyro_x = hub.imu()->gyro[0];
        s.gyro_y = hub.imu()->gyro[1];
        s.gyro_z = hub.imu()->gyro[2];
    }

    s.wheel_speed_fl = hub.pulseWheelFL().getFrequencyHz();
    s.wheel_speed_fr = hub.pulseWheelFR().getFrequencyHz();
    s.wheel_speed_rl = hub.pulseWheelRL().getFrequencyHz();
    s.wheel_speed_rr = hub.pulseWheelRR().getFrequencyHz();
    s.rpm = hub.pulseEngine().getFrequencyHz();

    s.wheel_count_fl = hub.pulseWheelFL().count();
    s.wheel_count_fr = hub.pulseWheelFR().count();
    s.wheel_count_rl = hub.pulseWheelRL().count();
    s.wheel_count_rr = hub.pulseWheelRR().count();
    s.rpm_count = hub.pulseEngine().count();

    s.gps_raw = hub.gps().getRawValue();
    s.gear = hub.gps().getGear();

    s.clutch_raw = hub.clutch().getRawValue();
    s.clutch = (float)hub.clutch().convertedValue();
    s.clutch_rpm = hub.pulseClutchRpm().getFrequencyHz();

    st.has_etc = true;
    dc_EtcState& e = st.etc;
    e.mode = targetModeToProto(hub.target());
    e.manual = hub.target().isManual();
    e.ittr = hub.target().isIttr();
    e.valid = isValid;

    // Build error bitmask
    e.errors = errorHandler.bits();

    sendDeviceMessage(env);
}

void SerialProtocol::sendDebugv(const char* fmt, va_list args) {
    dc_DeviceToHost env = dc_DeviceToHost_init_zero;
    env.which_payload = dc_DeviceToHost_debug_tag;
    dc_DebugMessage& d = env.payload.debug;
    d.timestamp = millis();

    vsnprintf(d.msg, sizeof(d.msg), fmt, args);

    sendDeviceMessage(env);
}

void SerialProtocol::sendDebugf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    sendDebugv(fmt, args);
    va_end(args);
}

void SerialProtocol::sendResponse(uint32_t id, bool ok) {
    dc_Response resp = dc_Response_init_zero;
    resp.id = id;
    resp.ok = ok;
    resp.which_data = 0;  // no payload
    sendResponseInternal(resp);
}

#define DEFINE_RESP_HELPER(NAME, TAG, FIELD, TYPE)                   \
    void SerialProtocol::NAME(uint32_t id, bool ok, const TYPE& p) { \
        dc_Response resp = dc_Response_init_zero;                    \
        resp.id = id;                                                \
        resp.ok = ok;                                                \
        resp.which_data = TAG;                                       \
        resp.data.FIELD = p;                                         \
        sendResponseInternal(resp);                                  \
    }

DEFINE_RESP_HELPER(sendResponseWithConfig, dc_Response_config_tag, config, dc_ConfigResponse)
DEFINE_RESP_HELPER(sendResponseWithAppsMin, dc_Response_apps_min_tag, apps_min, dc_AppsMinResponse)
DEFINE_RESP_HELPER(sendResponseWithAppsMax, dc_Response_apps_max_tag, apps_max, dc_AppsMaxResponse)
DEFINE_RESP_HELPER(sendResponseWithTpsMin, dc_Response_tps_min_tag, tps_min, dc_TpsMinResponse)
DEFINE_RESP_HELPER(sendResponseWithTpsMax, dc_Response_tps_max_tag, tps_max, dc_TpsMaxResponse)
DEFINE_RESP_HELPER(sendResponseWithIdling, dc_Response_idling_tag, idling, dc_IdlingResponse)
DEFINE_RESP_HELPER(sendResponseWithTargetBound, dc_Response_target_bound_tag, target_bound, dc_TargetBoundResponse)
DEFINE_RESP_HELPER(sendResponseWithIttr, dc_Response_ittr_tag, ittr, dc_IttrResponse)
DEFINE_RESP_HELPER(sendResponseWithFlags,
                   dc_Response_plausibility_flags_tag,
                   plausibility_flags,
                   dc_EtcPlausibilityCheckFlags)
DEFINE_RESP_HELPER(sendResponseWithPid, dc_Response_pid_tag, pid, dc_EtcPid)
DEFINE_RESP_HELPER(sendResponseWithCurve, dc_Response_target_curve_tag, target_curve, dc_EtcTargetCurve)
DEFINE_RESP_HELPER(sendResponseWithGpsGear, dc_Response_gps_gear_tag, gps_gear, dc_GpsGearResponse)

#undef DEFINE_RESP_HELPER

bool SerialProtocol::readCommand(dc_Command& out) {
    while (Serial.available() > 0) {
        int b = Serial.read();
        if (b < 0)
            break;

        if (b != 0x00) {
            if (rxLen < sizeof(rxBuf)) {
                rxBuf[rxLen++] = (uint8_t)b;
            } else {
                rxLen = 0;  // overflow: discard frame
            }
            continue;
        }

        // 0x00 boundary -> attempt to decode current buffer as one frame.
        if (rxLen == 0)
            continue;
        size_t cur_len = rxLen;
        rxLen = 0;

        uint8_t decoded[DC_MAX_PAYLOAD + 4];
        size_t n = cobs::decode(rxBuf, cur_len, decoded);
        if (n < 2)
            continue;  // need CRC

        size_t payload_len = n - 2;
        uint16_t recv_crc = (uint16_t)decoded[payload_len] | ((uint16_t)decoded[payload_len + 1] << 8);
        uint16_t calc_crc = crc16_ccitt(decoded, payload_len);
        if (recv_crc != calc_crc)
            continue;

        dc_HostToDevice msg = dc_HostToDevice_init_zero;
        pb_istream_t stream = pb_istream_from_buffer(decoded, payload_len);
        if (!pb_decode(&stream, dc_HostToDevice_fields, &msg))
            continue;

        if (msg.which_payload == dc_HostToDevice_command_tag) {
            out = msg.payload.command;
            return true;
        }
    }
    return false;
}
