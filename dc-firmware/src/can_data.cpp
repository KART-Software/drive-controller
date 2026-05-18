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
    if (msg.id == CAN_ID_MODE_SELECT && msg.len >= 1) {
        switch (static_cast<EtcMode>(msg.buf[0])) {
            case EtcMode::CALIB:
            case EtcMode::NORMAL:
            case EtcMode::RESTRICTED:
            case EtcMode::MOTOR_OFF:
                etcMode = static_cast<EtcMode>(msg.buf[0]);
                break;
            default:
                etcMode = EtcMode::NORMAL;
                break;
        }
        lastModeFrameMs = millis();
    }
    if (msg.id == CAN_ID_LAUNCH_CTRL && msg.len >= 1) {
        launchActive = (msg.buf[0] == 0x01);
        lastLaunchFrameMs = millis();
    }
}

void CanRxData::checkTimeouts(unsigned long nowMs) {
    if (launchActive && (nowMs - lastLaunchFrameMs) > CAN_LAUNCH_TIMEOUT_MS) {
        launchActive = false;
    }
    // 一度でも MODE_SELECT を受信していて、それが途絶した場合のみ NORMAL へフォールバック。
    // lastModeFrameMs == 0 (起動以来未受信) は判定スキップ — デフォルトの NORMAL のまま。
    // MOTOR_OFF は安全ラッチ: CAN 断で勝手にモーターを復帰させない。
    if (etcMode != EtcMode::NORMAL && etcMode != EtcMode::MOTOR_OFF && lastModeFrameMs != 0 &&
        (nowMs - lastModeFrameMs) > CAN_MODE_TIMEOUT_MS) {
        etcMode = EtcMode::NORMAL;
    }
}
