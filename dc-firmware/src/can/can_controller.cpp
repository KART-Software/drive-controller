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

}

void CanController::sendStatus(const CanStatusData& status) {
    bus_.send(status);
}

void CanController::poll() {
    // rxData_ は前回値を保持し、受信フレームのみ mergeFrame() で上書きする
    while (auto msg = bus_.read())
        rxData_.mergeFrame(*msg);
    // 途絶時のフォールバックは ControlInput が行う
}
