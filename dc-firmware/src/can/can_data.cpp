#include "can_data.hpp"
#include <kart_can.h>
#include "constants.hpp"

CanEtcMode selectToEtcMode(SelectSwitch3Pin::Status s) {
    switch (s) {
        case SelectSwitch3Pin::Status::First:
            return CanEtcMode::NORMAL;
        case SelectSwitch3Pin::Status::Second:
            return CanEtcMode::RESTRICTED;
        case SelectSwitch3Pin::Status::Third:
            return CanEtcMode::MOTOR_OFF;
        case SelectSwitch3Pin::Status::Zero:
        default:
            return CanEtcMode::CALIB;
    }
}

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

void CanRxData::mergeFrame(const CAN_message_t& msg) {
#if defined(CONTROL_INPUT_VIA_CAN)
    // 制御フレーム(0x740): byte0=mode, byte1=launch, byte2=auto-shift
    if (msg.id == KART_CAN_CONTROL_FRAME_ID && msg.len >= KART_CAN_CONTROL_LENGTH) {
        const auto c = unpackFrame(msg, kart_can_control_unpack);
        switch (static_cast<CanEtcMode>(c.etc_mode)) {
            case CanEtcMode::CALIB:
            case CanEtcMode::NORMAL:
            case CanEtcMode::RESTRICTED:
            case CanEtcMode::MOTOR_OFF:
                etcMode = static_cast<CanEtcMode>(c.etc_mode);
                break;
            default:
                // UNSPECIFIED (0) や未知値: モードは変更しない (現在値を維持)。
                // フレーム自体は受信できているので lastControlFrameMs だけ更新する。
                break;
        }
        launchActive = (c.launch_active == 0x01);
        autoShiftActive = (c.auto_shift == 0x01);
        lastControlFrameMs = millis();
    }
#else
    // CAN 制御入力は一旦凍結 (GPIO 直入力を使用, main.cpp)。0x740 は無視し、
    // etcMode/launchActive/autoShiftActive はデフォルト(安全側)のまま保持する。
    (void)msg;
#endif
}

void CanRxData::checkTimeouts(unsigned long nowMs) {
#if defined(CONTROL_INPUT_VIA_CAN)
    // 制御フレームが起動以来未受信 (== 0) なら判定スキップ — 各値はデフォルトのまま。
    if (lastControlFrameMs == 0 || (nowMs - lastControlFrameMs) <= CAN_CONTROL_TIMEOUT_MS) {
        return;
    }
    // launch 途絶 → false
    launchActive = false;
    // mode 途絶 → NORMAL。MOTOR_OFF は安全ラッチ: CAN 断で勝手にモーターを復帰させない。
    if (etcMode != CanEtcMode::MOTOR_OFF) {
        etcMode = CanEtcMode::NORMAL;
    }
    // auto-shift 途絶 → OFF(manual)。手動シフトが残る方が安全。
    autoShiftActive = false;
#else
    (void)nowMs;  // CAN 制御入力 凍結中はフォールバック不要 (GPIO 直入力が常に現在値)。
#endif
}
