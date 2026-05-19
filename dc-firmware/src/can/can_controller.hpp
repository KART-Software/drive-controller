#pragma once

#include "can_bus.hpp"
#include "can_data.hpp"
#include "sensor/sensor_hub.hpp"

class CanController {
   public:
    explicit CanController(const SensorHub& sensorHub);
    void begin();
    void send();  // Call at 60Hz: build CanTxData from sensorHub and send
    void poll();  // Drain RX FIFO into rxData_
    const CanRxData& rxData() const { return rxData_; }

   private:
    const SensorHub& sensorHub_;
    CanBus bus_;
    CanRxData rxData_;
};
