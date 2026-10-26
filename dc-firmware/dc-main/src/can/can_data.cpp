#include "can_data.hpp"
#include <kart_can.h>

namespace {

template <typename Msg>
CanFrame makeFrame(uint32_t id, uint8_t len, const Msg& m, int (*pack)(uint8_t*, const Msg*, size_t)) {
    CanFrame f = {.id = id, .len = len};
    pack(f.data, &m, sizeof(f.data));
    return f;
}

// kart-can の unpack 関数でフレームを構造体にして返す (C の出力引数はここに閉じ込める)
template <typename Msg>
Msg unpackFrame(const CanFrame& f, int (*unpack)(Msg*, const uint8_t*, size_t)) {
    Msg m = {};
    unpack(&m, f.data, f.len);
    return m;
}

// kart-can の etc_mode の値 (生成ヘッダに enum は無い。0 = UNSPECIFIED)
enum class CanEtcMode : uint8_t {
    CALIB = 1u,
    NORMAL = 2u,
    RESTRICTED = 3u,
    MOTOR_OFF = 4u,
};

std::optional<EtcTarget::Mode> modeFromCan(uint8_t v) {
    switch (static_cast<CanEtcMode>(v)) {
        case CanEtcMode::CALIB:
            return EtcTarget::Mode::Calibration;
        case CanEtcMode::NORMAL:
            return EtcTarget::Mode::Normal;
        case CanEtcMode::RESTRICTED:
            return EtcTarget::Mode::Restricted;
        case CanEtcMode::MOTOR_OFF:
            return EtcTarget::Mode::MotorOff;
    }
    return std::nullopt;  // UNSPECIFIED (0) や未知値
}

CanEtcMode modeToCan(EtcTarget::Mode m) {
    switch (m) {
        case EtcTarget::Mode::Calibration:
            return CanEtcMode::CALIB;
        case EtcTarget::Mode::Normal:
            return CanEtcMode::NORMAL;
        case EtcTarget::Mode::Restricted:
            return CanEtcMode::RESTRICTED;
        case EtcTarget::Mode::MotorOff:
            return CanEtcMode::MOTOR_OFF;
    }
    return CanEtcMode::MOTOR_OFF;
}

}  // namespace

std::array<CanFrame, CanImuData::FRAME_COUNT> CanImuData::toFrames() const {
    return {
        makeFrame(KART_CAN_DC_GYRO_XY_FRAME_ID, KART_CAN_DC_GYRO_XY_LENGTH,
                  kart_can_dc_gyro_xy_t{.gyro_x = gyro[0], .gyro_y = gyro[1]}, kart_can_dc_gyro_xy_pack),
        makeFrame(KART_CAN_DC_GYRO_Z_GEAR_FRAME_ID, KART_CAN_DC_GYRO_Z_GEAR_LENGTH,
                  kart_can_dc_gyro_z_gear_t{.gyro_z = gyro[2], .gear = static_cast<uint8_t>(gear)},  // 不明 (-1) は 0xFF
                  kart_can_dc_gyro_z_gear_pack),
        makeFrame(KART_CAN_DC_ACCEL_XY_FRAME_ID, KART_CAN_DC_ACCEL_XY_LENGTH,
                  kart_can_dc_accel_xy_t{.accel_x = accel[0], .accel_y = accel[1]}, kart_can_dc_accel_xy_pack),
        makeFrame(KART_CAN_DC_ACCEL_Z_FRAME_ID, KART_CAN_DC_ACCEL_Z_LENGTH,
                  kart_can_dc_accel_z_t{.accel_z = accel[2]}, kart_can_dc_accel_z_pack),
    };
}

namespace {

// 物理値 → kart-can の uint16 (scale 付き)。範囲外は端に丸める (cantools の encode は丸めも飽和もしない)
uint16_t scaled(float value, float scale) {
    const float v = value / scale + 0.5f;
    if (!(v > 0.0f))  // 負と NaN
        return 0;
    return v >= 65535.0f ? 65535 : (uint16_t)v;
}

}  // namespace

std::array<CanFrame, CanSensorData::FRAME_COUNT> CanSensorData::toFrames() const {
    return {
        makeFrame(KART_CAN_DC_PEDALS_FRAME_ID, KART_CAN_DC_PEDALS_LENGTH,
                  kart_can_dc_pedals_t{
                      .apps = scaled(appsPercent, 0.01f),
                      .tps = scaled(tpsPercent, 0.01f),
                      .clutch = scaled(clutchPercent, 0.01f),
                      .brake_press_rear = scaled(brakeRearPsi, 0.1f),
                  },
                  kart_can_dc_pedals_pack),
        // 生値はカウントのまま (受け側が 7.8125e-5 V/count で換算)
        makeFrame(KART_CAN_DC_ETC_RAW_FRAME_ID, KART_CAN_DC_ETC_RAW_LENGTH,
                  kart_can_dc_etc_raw_t{.apps_1 = appsRaw[0], .apps_2 = appsRaw[1], .tps_1 = tpsRaw[0], .tps_2 = tpsRaw[1]},
                  kart_can_dc_etc_raw_pack),
        makeFrame(KART_CAN_DC_WHEEL_SPEED_FRAME_ID, KART_CAN_DC_WHEEL_SPEED_LENGTH,
                  kart_can_dc_wheel_speed_t{
                      .wheel_fl = scaled(wheelKmh[0], 0.01f),
                      .wheel_fr = scaled(wheelKmh[1], 0.01f),
                      .wheel_rl = scaled(wheelKmh[2], 0.01f),
                      .wheel_rr = scaled(wheelKmh[3], 0.01f),
                  },
                  kart_can_dc_wheel_speed_pack),
        // 累計は uint16 でラップして送る (受け側が差分を取る)
        makeFrame(KART_CAN_DC_WHEEL_PULSE_FRAME_ID, KART_CAN_DC_WHEEL_PULSE_LENGTH,
                  kart_can_dc_wheel_pulse_t{
                      .pulse_fl = (uint16_t)wheelPulses[0],
                      .pulse_fr = (uint16_t)wheelPulses[1],
                      .pulse_rl = (uint16_t)wheelPulses[2],
                      .pulse_rr = (uint16_t)wheelPulses[3],
                  },
                  kart_can_dc_wheel_pulse_pack),
        makeFrame(KART_CAN_DC_ENGINE_FRAME_ID, KART_CAN_DC_ENGINE_LENGTH,
                  kart_can_dc_engine_t{.engine_rpm = scaled(engineRpm, 1.0f), .clutch_rpm = scaled(clutchRpm, 1.0f)},
                  kart_can_dc_engine_pack),
    };
}

CanFrame CanStatusData::toFrame() const {
    return makeFrame(KART_CAN_DC_STATUS_FRAME_ID, KART_CAN_DC_STATUS_LENGTH,
                     kart_can_dc_status_t{
                         .etc_mode = static_cast<uint8_t>(modeToCan(etcMode)),
                         .launch_active = launchActive,
                         .auto_shift = autoShift,
                         .starter_relay = starterRelay,
                         .shutdown_loop = shutdownLoopClosed,
                     },
                     kart_can_dc_status_pack);
}

void CanRxData::mergeFrame(const CanFrame& msg, uint32_t nowMs) {
    // Control (0x740): byte0 etc_mode / byte1 launch / byte2 auto_shift / byte3 starter。
    // byte0-2 は旧 DLC 3 フレームと互換なので len >= 3 で受け、starter は len >= 4 のときだけ読む。
    if (msg.id == KART_CAN_CONTROL_FRAME_ID && msg.len >= 3) {
        const auto c = unpackFrame(msg, kart_can_control_unpack);
        etcMode = modeFromCan(c.etc_mode);
        launchActive = (c.launch_active == 0x01);
        autoShiftActive = (c.auto_shift == 0x01);
        starter = (msg.len >= 4) && (c.starter == 0x01);
        controlFrames++;
        lastControlFrameMs = nowMs;
        return;
    }
    if (msg.id == KART_CAN_SHIFT_FRAME_ID && msg.len >= KART_CAN_SHIFT_LENGTH) {
        const auto sh = unpackFrame(msg, kart_can_shift_unpack);
        shiftUp = (sh.shift_up == 0x01);
        shiftDown = (sh.shift_down == 0x01);
        shiftFrames++;
        lastShiftFrameMs = nowMs;
    }
}

bool needsImmediateHandling(const CanFrame& msg) {
    return msg.id == KART_CAN_SHIFT_FRAME_ID && msg.len >= KART_CAN_SHIFT_LENGTH;
}
