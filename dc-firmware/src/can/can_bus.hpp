#pragma once

#include <Arduino.h>
#include <FlexCAN_T4.h>

#include "can_data.hpp"

class CanBus {
   public:
    // 受信は割り込み (FIFO 割り込み)。onRx は FlexCAN の受信割り込みの中から直接呼ばれる
    // (events() を呼ばないとライブラリはキューを経由せずコールバックを呼ぶ)。priority は NVIC 優先度
    void begin(void (*onRx)(const CAN_message_t& msg), uint8_t priority);

    // Send all 60Hz TX frames built from data
    void send(const CanTxData& data);

    // このノードの状態 (0x60A DC_Status)
    void send(const CanStatusData& status);


   private:
    // CAN3 = Teensy 4.1 の pin 30/31 (CAN1 の 22/23 はモーター PWM/DIR に割当のため)
    FlexCAN_T4<CAN3, RX_SIZE_256, TX_SIZE_16> can1;
};
