#include "can_controller.hpp"

CanController::CanController(const SensorHub& sensorHub) : sensorHub_(sensorHub) {}

void CanController::begin(void (*onRx)(const CAN_message_t& msg), uint8_t priority) {
    bus_.begin(onRx, priority);
}

void CanController::send() {
    const Imu::Vec3 gyro = sensorHub_.imu().gyro();
    const Imu::Vec3 accel = sensorHub_.imu().accel();
    CanTxData data;
    data.gear = sensorHub_.gps().getGear();
    for (int i = 0; i < 3; i++) {
        data.gyro[i] = gyro[i];
        data.accel[i] = accel[i];
    }
    bus_.send(data);

}

void CanController::sendStatus(const CanStatusData& status) {
    bus_.send(status);
}
