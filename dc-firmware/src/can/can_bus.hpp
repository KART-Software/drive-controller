#pragma once

#include <Arduino.h>
#include <FlexCAN_T4.h>
#include "can_data.hpp"

class CanBus {
   public:
    void begin();

    // Send all 60Hz TX frames built from data
    void send(const CanTxData& data);

    // Poll RX FIFO and merge all received frames into rx.
    void poll(CanRxData& rx);

   private:
    FlexCAN_T4<CAN1, RX_SIZE_256, TX_SIZE_16> can1;
};
