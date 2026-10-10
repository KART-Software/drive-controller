#include "can_bus.hpp"
#include <kart_can.h>
#include "constants.hpp"

void CanBus::begin() {
    can1.begin();
    can1.setBaudRate(CAN_BITRATE);
    can1.enableFIFO();
}

void CanBus::send(const CanTxData& data) {
    for (const auto& f : data.toFrames())
        can1.write(f);
}

void CanBus::send(const CanStatusData& status) {
    can1.write(status.toFrame());
}

std::optional<CAN_message_t> CanBus::read() {
    CAN_message_t msg;  // FlexCAN の read は出力引数なので、ここで値に変換する
    if (can1.read(msg))
        return msg;
    return std::nullopt;
}
