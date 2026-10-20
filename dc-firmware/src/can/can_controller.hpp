#pragma once

#include "can_data.hpp"
#include "interfaces/can/can_bus.hpp"
#include "sensor/sensor_hub.hpp"

// CAN の送受信の内容を決める層。バスは CanBus (interface) で受け取り、ハードウェアは知らない。
class CanController {
   public:
    CanController(const SensorHub& sensorHub, CanBus& bus);
    // 受けるのは 0x740 / 0x741 だけ (ハードウェアのフィルタ)。onRx は受信割り込みから 1 フレームごとに呼ばれる。
    // 中で receive() を呼ぶこと。false = バスがこの設定 (CAN_BITRATE、classic) に対応していない
    bool begin(CanBus::RxCallback onRx, uint8_t priority);
    void send();                                   // 0x600-0x603 (tickISR, KART_CAN_DC_GYRO_XY_CYCLE_TIME_MS)
    void sendStatus(const CanStatusData& status);  // 0x60A (tickISR, KART_CAN_DC_STATUS_CYCLE_TIME_MS)
    // 受信フレームを rxData_ に取り込む (canRxISR)。nowMs = 受信時刻 (sysClock の ms)
    void receive(const CanFrame& frame, uint32_t nowMs) { rxData_.mergeFrame(frame, nowMs); }
    const CanRxData& rxData() const { return rxData_; }
    CanState busState() const { return bus_.state(); }
    CanStats busStats() const { return bus_.stats(); }

   private:
    const SensorHub& sensorHub_;
    CanBus& bus_;
    CanRxData rxData_;
};
