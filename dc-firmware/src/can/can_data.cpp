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

void CanTxData::toFrames(CAN_message_t (&out)[FRAME_COUNT]) const {
    // 0x600: gyro x/y (float32 LE)
    out[0] = {};
    out[0].id = KART_CAN_DC_GYRO_XY_FRAME_ID;
    out[0].len = KART_CAN_DC_GYRO_XY_LENGTH;
    struct kart_can_dc_gyro_xy_t m0 = {};
    m0.gyro_x = gyro[0];
    m0.gyro_y = gyro[1];
    kart_can_dc_gyro_xy_pack(out[0].buf, &m0, sizeof(out[0].buf));

    // 0x601: gyro z (float32 LE) + gear (u8)
    out[1] = {};
    out[1].id = KART_CAN_DC_GYRO_Z_GEAR_FRAME_ID;
    out[1].len = KART_CAN_DC_GYRO_Z_GEAR_LENGTH;
    struct kart_can_dc_gyro_z_gear_t m1 = {};
    m1.gyro_z = gyro[2];
    m1.gear = static_cast<uint8_t>(gear);  // -1=unknown → 0xFF
    kart_can_dc_gyro_z_gear_pack(out[1].buf, &m1, sizeof(out[1].buf));

    // 0x602: accel x/y (float32 LE)
    out[2] = {};
    out[2].id = KART_CAN_DC_ACCEL_XY_FRAME_ID;
    out[2].len = KART_CAN_DC_ACCEL_XY_LENGTH;
    struct kart_can_dc_accel_xy_t m2 = {};
    m2.accel_x = accel[0];
    m2.accel_y = accel[1];
    kart_can_dc_accel_xy_pack(out[2].buf, &m2, sizeof(out[2].buf));

    // 0x603: accel z (float32 LE)
    out[3] = {};
    out[3].id = KART_CAN_DC_ACCEL_Z_FRAME_ID;
    out[3].len = KART_CAN_DC_ACCEL_Z_LENGTH;
    struct kart_can_dc_accel_z_t m3 = {};
    m3.accel_z = accel[2];
    kart_can_dc_accel_z_pack(out[3].buf, &m3, sizeof(out[3].buf));
}

void CanRxData::mergeFrame(const CAN_message_t& msg) {
#if defined(CONTROL_INPUT_VIA_CAN)
    // 制御フレーム(0x740): byte0=mode, byte1=launch, byte2=auto-shift
    if (msg.id == KART_CAN_CONTROL_FRAME_ID && msg.len >= KART_CAN_CONTROL_LENGTH) {
        struct kart_can_control_t c;
        kart_can_control_unpack(&c, msg.buf, msg.len);
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
