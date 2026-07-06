#include "can_data.hpp"
#include <cstring>
#include "constants.hpp"

static void packFloat(uint8_t* dst, float val) {
    memcpy(dst, &val, 4);
}

void CanTxData::toFrames(CAN_message_t (&out)[FRAME_COUNT]) const {
    out[0] = {};
    out[0].id = CAN_ID_GYRO_XY;
    out[0].len = 8;
    packFloat(out[0].buf + 0, gyro[0]);
    packFloat(out[0].buf + 4, gyro[1]);

    out[1] = {};
    out[1].id = CAN_ID_GYRO_Z_GEAR;
    out[1].len = 5;
    packFloat(out[1].buf + 0, gyro[2]);
    out[1].buf[4] = static_cast<uint8_t>(gear);

    out[2] = {};
    out[2].id = CAN_ID_ACCEL_XY;
    out[2].len = 8;
    packFloat(out[2].buf + 0, accel[0]);
    packFloat(out[2].buf + 4, accel[1]);

    out[3] = {};
    out[3].id = CAN_ID_ACCEL_Z;
    out[3].len = 4;
    packFloat(out[3].buf, accel[2]);
}

void CanRxData::mergeFrame(const CAN_message_t& msg) {
    // 制御フレーム(0x740): byte0=mode, byte1=launch, byte2=auto-shift
    if (msg.id == CAN_ID_CONTROL && msg.len >= 3) {
        switch (static_cast<CanEtcMode>(msg.buf[0])) {
            case CanEtcMode::CALIB:
            case CanEtcMode::NORMAL:
            case CanEtcMode::RESTRICTED:
            case CanEtcMode::MOTOR_OFF:
                etcMode = static_cast<CanEtcMode>(msg.buf[0]);
                break;
            default:
                // UNSPECIFIED (0) や未知値: モードは変更しない (現在値を維持)。
                // フレーム自体は受信できているので lastControlFrameMs だけ更新する。
                break;
        }
        launchActive = (msg.buf[1] == 0x01);
        autoShiftActive = (msg.buf[2] == 0x01);
        lastControlFrameMs = millis();
    }
}

void CanRxData::checkTimeouts(unsigned long nowMs) {
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
}
