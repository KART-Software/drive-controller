#pragma once

#include <Arduino.h>
#include <FlexCAN_T4.h>

#include <optional>
#include "can_data.hpp"

class CanBus {
   public:
    void begin();

    // Send all 60Hz TX frames built from data
    void send(const CanTxData& data);

    // GPIO 制御入力 (ETCモード / auto-shift) を Control(0x740) フレームとして CAN 出力。
    void sendControl(CanEtcMode mode, bool autoShift);

    // RX FIFO から 1 フレーム取り出す (空なら nullopt)
    std::optional<CAN_message_t> read();

   private:
    // CAN3 = Teensy 4.1 の pin 30/31 (CAN1 の 22/23 はモーター PWM/DIR に割当のため)
    FlexCAN_T4<CAN3, RX_SIZE_256, TX_SIZE_16> can1;
};
