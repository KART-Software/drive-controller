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
    }
}
