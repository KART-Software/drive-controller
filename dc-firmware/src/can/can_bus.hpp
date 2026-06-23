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
    // CAN3 = Teensy 4.1 の pin 30/31 (CAN1 の 22/23 はモーター PWM/DIR に割当のため)
    FlexCAN_T4<CAN3, RX_SIZE_256, TX_SIZE_16> can1;
};
