#include "wire.hpp"

#include <Arduino.h>
#include <Teensy4TinyUSB.h>
#include <pb_decode.h>
#include <pb_encode.h>

#include "proto/drive_controller.pb.h"
#include "serial/cobs.hpp"
#include "serial/crc16.hpp"

namespace {
constexpr size_t kMaxPayload = 768;  // dc-main の DC_MAX_PAYLOAD と同じ
constexpr size_t kMaxFrame = kMaxPayload + kMaxPayload / 254 + 2;
uint8_t rxBuf[kMaxFrame];
size_t rxLen = 0;
}  // namespace

int wire::pollReboot() {
    while (SerialTinyUSB.available() > 0) {
        const int b = SerialTinyUSB.read();
        if (b < 0)
            break;
        if (b != 0x00) {
            if (rxLen < sizeof(rxBuf))
                rxBuf[rxLen++] = (uint8_t)b;
            else
                rxLen = 0;
            continue;
        }
        if (rxLen == 0)
            continue;
        const size_t len = rxLen;
        rxLen = 0;

        uint8_t decoded[kMaxPayload + 4];
        const size_t n = cobs::decode(rxBuf, len, decoded);
        if (n < 2)
            continue;
        const size_t payloadLen = n - 2;
        const uint16_t recvCrc = (uint16_t)decoded[payloadLen] | ((uint16_t)decoded[payloadLen + 1] << 8);
        if (recvCrc != crc16_ccitt(decoded, payloadLen))
            continue;

        dc_HostToDevice msg = {};
        pb_istream_t stream = pb_istream_from_buffer(decoded, payloadLen);
        if (!pb_decode(&stream, dc_HostToDevice_fields, &msg) || msg.which_payload != dc_HostToDevice_command_tag)
            continue;
        const dc_Command& cmd = msg.payload.command;
        if (cmd.which_body == dc_Command_reboot_tag) {
            sendResponse(cmd.id, true);
            return cmd.body.reboot.mode == dc_RebootCmd_Mode_SD_READER ? 1 : 0;
        }
        sendResponse(cmd.id, false);  // このモードでは reboot 以外は受けない
    }
    return -1;
}

void wire::sendResponse(uint32_t id, bool ok) {
    const dc_DeviceToHost msg = {
        .which_payload = dc_DeviceToHost_response_tag,
        .payload = {.response = {.id = id, .ok = ok, .which_data = 0}},
    };
    uint8_t payload[64];
    pb_ostream_t stream = pb_ostream_from_buffer(payload, sizeof(payload));
    if (!pb_encode(&stream, dc_DeviceToHost_fields, &msg))
        return;
    const size_t len = stream.bytes_written;
    const uint16_t crc = crc16_ccitt(payload, len);
    uint8_t body[sizeof(payload) + 2];
    memcpy(body, payload, len);
    body[len] = (uint8_t)(crc & 0xFF);
    body[len + 1] = (uint8_t)(crc >> 8);
    uint8_t frame[sizeof(body) + sizeof(body) / 254 + 2];
    const size_t n = cobs::encode(body, len + 2, frame);
    frame[n] = 0x00;
    SerialTinyUSB.write(frame, n + 1);
    SerialTinyUSB.flush();  // 実際に送られるのは次の Teensy4USB::task() で
}
