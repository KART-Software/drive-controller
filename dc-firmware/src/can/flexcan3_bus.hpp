#pragma once

#include <stdint.h>

#include "interfaces/can/can_bus.hpp"

// Teensy 4.1 の CAN3 (FlexCAN3、pin 30 = CRX3 / pin 31 = CTX3) の CanBus 実装 (docs/can_bus_spec.md §4)。
// レジスタは RM (i.MX RT1060) Chapter 45 の定義を自前で持ち、ライブラリは使わない。
// - 受信: ID ごとに受信メールボックスを 1 本 (個別マスクで一致)。割り込みはそのメールボックスのフラグだけ。
//   ISR は届いたメールボックスを読んでコールバックを呼び、末尾で ESR1 のフラグを消す。
//   (Rx FIFO は CAN FD で使えないので使わない。§4.8)
// - 送信: 受信の後ろのメールボックスの空きに書く。送信完了の割り込みもキューも持たない。
// - エラー: 割り込みは使わず state() で読む。バスオフは FlexCAN の自動復帰に任せる。
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
