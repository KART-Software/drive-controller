#pragma once

#include <stdint.h>

#include "interfaces/can/can_bus.hpp"

// Teensy 4.1 の CAN3 (FlexCAN3、pin 30 = CRX3 / pin 31 = CTX3) の CanBus 実装 (docs/can_bus_spec.md §4)。
// レジスタは RM (i.MX RT1060) Chapter 45 の定義を自前で持ち、ライブラリは使わない。受信は ID ごとのメールボックス
// (Rx FIFO は CAN FD で使えないので使わない、§4.8)、送信はキューなし、エラー割り込みなし (バスオフは自動復帰に任せる)。
// 単一インスタンス (ISR から届くための静的ポインタは .cpp の中)。
class Flexcan3Bus final : public CanBus {
   public:
    bool begin(const CanConfig& config, const CanRxFilters& filters, RxCallback onRx, uint8_t irqPriority) override;
    bool send(const CanFrame& frame) override;
    CanState state() const override;
    CanStats stats() const override;

   private:
    static void isr();
    void handleInterrupt();

    RxCallback onRx_ = nullptr;
    uint8_t rxMbCount_ = 0;      // 受信メールボックスの数 (= フィルタの数)。送信はその後ろ
    uint32_t rxMbMask_ = 0;      // 受信メールボックスの IFLAG1 / IMASK1 のビット
    volatile uint32_t rxFrames_ = 0;   // ISR が数える
    volatile uint32_t rxOverrun_ = 0;  // ISR が数える
    volatile uint32_t txDropped_ = 0;  // send() (tickISR) が数える
    volatile uint32_t isrCount_ = 0;
};
