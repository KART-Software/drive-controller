#pragma once

#include "can_bus.hpp"
#include "can_data.hpp"
#include "sensor/sensor_hub.hpp"

class CanController {
   public:
    explicit CanController(const SensorHub& sensorHub);
    // onRx は受信割り込み (canRxISR) から 1 フレームごとに呼ばれる。中で receive() を呼ぶこと
    void begin(void (*onRx)(const CAN_message_t& msg), uint8_t priority);
    void send();                                   // 0x600-0x603 (tickISR, KART_CAN_DC_GYRO_XY_CYCLE_TIME_MS)
    void sendStatus(const CanStatusData& status);  // 0x60A (tickISR, KART_CAN_DC_STATUS_CYCLE_TIME_MS)
    // 受信フレームを rxData_ に取り込む (canRxISR)。nowMs = 受信時刻 (sysClock の ms)
    void receive(const CAN_message_t& msg, uint32_t nowMs) { rxData_.mergeFrame(msg, nowMs); }
    const CanRxData& rxData() const { return rxData_; }

   private:
    const SensorHub& sensorHub_;
    CanBus bus_;
    CanRxData rxData_;
};
