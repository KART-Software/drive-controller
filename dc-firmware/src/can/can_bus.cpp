#include "can_bus.hpp"
#include <kart_can.h>
#include "constants.hpp"

void CanBus::begin(void (*onRx)(const CAN_message_t& msg), uint8_t priority) {
    can1.begin();
    can1.setBaudRate(CAN_BITRATE);
    can1.enableFIFO();
    can1.onReceive(onRx);
    can1.enableFIFOInterrupt();
    NVIC_SET_PRIORITY(IRQ_CAN3, priority);  // ライブラリは優先度を設定しない (Teensy 既定 128 のまま)
}

void CanBus::send(const CanTxData& data) {
    for (const auto& f : data.toFrames())
        can1.write(f);
}

void CanBus::send(const CanStatusData& status) {
    can1.write(status.toFrame());
}
