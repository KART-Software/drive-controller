#include "can_controller.hpp"

#include <kart_can.h>

#include "constants.hpp"

// 1 回の呼び出しでまとめて送るフレームは同じ周期であること (kart-can の can.yaml で片方だけ変えると黙ってずれる)
static_assert(KART_CAN_DC_GYRO_Z_GEAR_CYCLE_TIME_MS == KART_CAN_DC_GYRO_XY_CYCLE_TIME_MS &&
                  KART_CAN_DC_ACCEL_XY_CYCLE_TIME_MS == KART_CAN_DC_GYRO_XY_CYCLE_TIME_MS &&
                  KART_CAN_DC_ACCEL_Z_CYCLE_TIME_MS == KART_CAN_DC_GYRO_XY_CYCLE_TIME_MS,
              "0x600〜0x603 は sendImu() で一緒に送る");
static_assert(KART_CAN_DC_ETC_RAW_CYCLE_TIME_MS == KART_CAN_DC_PEDALS_CYCLE_TIME_MS &&
                  KART_CAN_DC_WHEEL_SPEED_CYCLE_TIME_MS == KART_CAN_DC_PEDALS_CYCLE_TIME_MS &&
                  KART_CAN_DC_WHEEL_PULSE_CYCLE_TIME_MS == KART_CAN_DC_PEDALS_CYCLE_TIME_MS &&
                  KART_CAN_DC_ENGINE_CYCLE_TIME_MS == KART_CAN_DC_PEDALS_CYCLE_TIME_MS,
              "0x604 / 0x605 / 0x607〜0x609 は sendSensors() で一緒に送る");

CanController::CanController(const SensorView& sensorHub, CanBus& bus) : sensorHub_(sensorHub), bus_(bus) {}

bool CanController::begin(CanBus::RxCallback onRx, uint8_t priority) {
    const CanConfig config = {.bitrate = CAN_BITRATE, .fd = false, .dataBitrate = 0};
    const CanRxFilters filters = {.ids = {KART_CAN_CONTROL_FRAME_ID, KART_CAN_SHIFT_FRAME_ID}, .count = 2};
    return bus_.begin(config, filters, onRx, priority);
}

void CanController::sendImu() {
    const Imu::Vec3 gyro = sensorHub_.imu().gyro();
    const Imu::Vec3 accel = sensorHub_.imu().accel();
    CanImuData data;
    data.gear = sensorHub_.gps().getGear();
    for (int i = 0; i < 3; i++) {
        data.gyro[i] = gyro[i];
        data.accel[i] = accel[i];
    }
    for (const CanFrame& f : data.toFrames())
        bus_.send(f);  // 空きがなければ捨てる (次の周期で送る)
}

void CanController::sendSensors() {
    const SensorView& v = sensorHub_;
    const CanSensorData data = {
        .appsPercent = (float)v.apps1().convertedValue(),
        .tpsPercent = (float)v.tps1().convertedValue(),
        .clutchPercent = (float)v.clutch().convertedValue(),
        .brakeRearPsi = (float)v.bps().convertedValue(),
        .appsRaw = {v.apps1().getRawValue(), v.apps2().getRawValue()},
        .tpsRaw = {v.tps1().getRawValue(), v.tps2().getRawValue()},
        .wheelKmh = {v.pulseWheelFL().kmh(), v.pulseWheelFR().kmh(), v.pulseWheelRL().kmh(), v.pulseWheelRR().kmh()},
        .wheelPulses = {v.pulseWheelFL().pulses(), v.pulseWheelFR().pulses(), v.pulseWheelRL().pulses(),
                        v.pulseWheelRR().pulses()},
        .engineRpm = v.pulseEngine().rpm(),
        .clutchRpm = v.pulseClutchRpm().rpm(),
    };
    for (const CanFrame& f : data.toFrames())
        bus_.send(f);  // 空きがなければ捨てる (次の周期で送る)
}

void CanController::sendStatus(const CanStatusData& status) {
    bus_.send(status.toFrame());
}
