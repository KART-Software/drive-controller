#include "can_controller.hpp"

#include "constants.hpp"

CanController::CanController(const SensorHub& sensorHub) : sensorHub_(sensorHub) {}

void CanController::begin() {
    bus_.begin();
}

void CanController::send() {
    const Imu* imu = sensorHub_.imu();
    CanTxData data;
    data.gear = sensorHub_.gps().getGear();
    data.gyro[0] = imu ? imu->gyro[0] : 0.0f;
    data.gyro[1] = imu ? imu->gyro[1] : 0.0f;
    data.gyro[2] = imu ? imu->gyro[2] : 0.0f;
    data.accel[0] = imu ? imu->accel[0] : 0.0f;
    data.accel[1] = imu ? imu->accel[1] : 0.0f;
    data.accel[2] = imu ? imu->accel[2] : 0.0f;
    bus_.send(data);

#if !defined(CONTROL_INPUT_VIA_CAN)
    // GPIO 制御入力 (mode / auto-shift) を Control(0x740) として CAN 出力する。
    // CONTROL_INPUT_VIA_CAN 定義時は逆に 0x740 を受信する側なので出力しない。
    bus_.sendControl(selectToEtcMode(sensorHub_.modeSwitch().getStatus()),
                     sensorHub_.autoShiftSwitch().isOn());
#endif
}

void CanController::poll() {
    // rxData_ は前回値を保持し、受信フレームのみ mergeFrame() で上書きする
    bus_.poll(rxData_);
    // LAUNCH_CTRL / MODE_SELECT / AUTO_SHIFT フレーム途絶時に各々を安全側へフォールバック (CAN 断対策)
    rxData_.checkTimeouts(millis());
}
