#include "can_controller.hpp"

#include <kart_can.h>

#include "constants.hpp"

CanController::CanController(const SensorHub& sensorHub, CanBus& bus) : sensorHub_(sensorHub), bus_(bus) {}

bool CanController::begin(CanBus::RxCallback onRx, uint8_t priority) {
    const CanConfig config = {.bitrate = CAN_BITRATE, .fd = false, .dataBitrate = 0};
    const CanRxFilters filters = {.ids = {KART_CAN_CONTROL_FRAME_ID, KART_CAN_SHIFT_FRAME_ID}, .count = 2};
    return bus_.begin(config, filters, onRx, priority);
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
    for (const CanFrame& f : data.toFrames())
        bus_.send(f);  // 空きがなければ捨てる (次の周期で送る)
}

void CanController::sendStatus(const CanStatusData& status) {
    bus_.send(status.toFrame());
}
