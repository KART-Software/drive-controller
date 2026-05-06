#include "can_controller.hpp"

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

void CanController::poll() {
    rxData_ = {};
    bus_.poll(rxData_);
}
