#include "can_bus.hpp"
#include <kart_can.h>
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

void CanBus::poll(CanRxData& rx) {
    CAN_message_t msg;
    while (can1.read(msg)) {
        rx.mergeFrame(msg);
    }
}
