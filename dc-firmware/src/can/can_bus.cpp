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

void CanBus::sendControl(CanEtcMode mode, bool autoShift) {
    struct kart_can_control_t c = {};
    c.etc_mode = static_cast<uint8_t>(mode);
    c.launch_active = 0;  // launch は凍結中 (GPIO 入力なし)
    c.auto_shift = autoShift ? 1 : 0;
    CAN_message_t msg = {};
    msg.id = KART_CAN_CONTROL_FRAME_ID;
    msg.len = KART_CAN_CONTROL_LENGTH;
    kart_can_control_pack(msg.buf, &c, sizeof(msg.buf));
    can1.write(msg);
}

std::optional<CAN_message_t> CanBus::read() {
    CAN_message_t msg;  // FlexCAN の read は出力引数なので、ここで値に変換する
    if (can1.read(msg))
        return msg;
    return std::nullopt;
}
