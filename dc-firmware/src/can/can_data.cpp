#include "can_data.hpp"
#include <kart_can.h>
#include "constants.hpp"

namespace {

// kart-can の pack 関数でペイロードを詰めたフレームを返す
template <typename Msg>
CAN_message_t makeFrame(uint32_t id, uint8_t len, const Msg& m, int (*pack)(uint8_t*, const Msg*, size_t)) {
    CAN_message_t f = {.id = id, .len = len};
    pack(f.buf, &m, sizeof(f.buf));
    return f;
}

// kart-can の unpack 関数でフレームを構造体にして返す (C の出力引数はここに閉じ込める)
template <typename Msg>
Msg unpackFrame(const CAN_message_t& f, int (*unpack)(Msg*, const uint8_t*, size_t)) {
    Msg m = {};
    unpack(&m, f.buf, f.len);
    return m;
}

// kart-can の etc_mode の値 (1=CALIB 2=NORMAL 3=RESTRICTED 4=MOTOR_OFF、0=UNSPECIFIED)
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

std::array<CAN_message_t, CanTxData::FRAME_COUNT> CanTxData::toFrames() const {
    return {
        // 0x600: gyro x/y (float32 LE)
        makeFrame(KART_CAN_DC_GYRO_XY_FRAME_ID, KART_CAN_DC_GYRO_XY_LENGTH,
                  kart_can_dc_gyro_xy_t{.gyro_x = gyro[0], .gyro_y = gyro[1]}, kart_can_dc_gyro_xy_pack),
        // 0x601: gyro z (float32 LE) + gear (u8, -1=unknown → 0xFF)
        makeFrame(KART_CAN_DC_GYRO_Z_GEAR_FRAME_ID, KART_CAN_DC_GYRO_Z_GEAR_LENGTH,
                  kart_can_dc_gyro_z_gear_t{.gyro_z = gyro[2], .gear = static_cast<uint8_t>(gear)},
                  kart_can_dc_gyro_z_gear_pack),
        // 0x602: accel x/y (float32 LE)
        makeFrame(KART_CAN_DC_ACCEL_XY_FRAME_ID, KART_CAN_DC_ACCEL_XY_LENGTH,
                  kart_can_dc_accel_xy_t{.accel_x = accel[0], .accel_y = accel[1]}, kart_can_dc_accel_xy_pack),
        // 0x603: accel z (float32 LE)
        makeFrame(KART_CAN_DC_ACCEL_Z_FRAME_ID, KART_CAN_DC_ACCEL_Z_LENGTH,
                  kart_can_dc_accel_z_t{.accel_z = accel[2]}, kart_can_dc_accel_z_pack),
    };
}

CAN_message_t CanStatusData::toFrame() const {
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

void CanRxData::mergeFrame(const CAN_message_t& msg) {
    // Control (0x740): byte0 etc_mode / byte1 launch / byte2 auto_shift / byte3 starter。
    // byte0-2 は旧 DLC 3 フレームと互換なので len >= 3 で受け、starter は len >= 4 のときだけ読む。
    if (msg.id == KART_CAN_CONTROL_FRAME_ID && msg.len >= 3) {
        const auto c = unpackFrame(msg, kart_can_control_unpack);
        etcMode = modeFromCan(c.etc_mode);
        launchActive = (c.launch_active == 0x01);
        autoShiftActive = (c.auto_shift == 0x01);
        starter = (msg.len >= 4) && (c.starter == 0x01);
        controlFrames++;
        lastControlFrameMs = millis();
        return;
    }
    // Shift (0x741): byte0 shift_up / byte1 shift_down (押下中 1)
    if (msg.id == KART_CAN_SHIFT_FRAME_ID && msg.len >= KART_CAN_SHIFT_LENGTH) {
        const auto sh = unpackFrame(msg, kart_can_shift_unpack);
        shiftUp = (sh.shift_up == 0x01);
        shiftDown = (sh.shift_down == 0x01);
        shiftFrames++;
        lastShiftFrameMs = millis();
    }
}
