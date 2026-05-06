#include "can_bus.hpp"
#include "constants.hpp"

void CanBus::begin() {
    can1.begin();
    can1.setBaudRate(CAN_BITRATE);
    can1.enableFIFO();
}

void CanBus::send(const CanTxData& data) {
    CAN_message_t frames[CanTxData::FRAME_COUNT];
    data.toFrames(frames);
    for (const auto& f : frames)
        can1.write(f);
}

void CanBus::poll(CanRxData& rx) {
    CAN_message_t msg;
    while (can1.read(msg)) {
        rx.mergeFrame(msg);
    }
}
