#pragma once

#include <stdint.h>

// ホストとのシリアル (Teensy4TinyUSB の CDC、SerialTinyUSB。dc-main と同じワイヤフォーマット: COBS( protobuf ‖ crc16_le ) 0x00)。
// このファームが受けるのは RebootCmd だけ、送るのは Response だけ。
namespace wire {
// 受信した RebootCmd の mode を返す (0 = NORMAL, 1 = SD_READER)。無ければ -1。ほかのコマンドには ok=false を返して捨てる
int pollReboot();
void sendResponse(uint32_t id, bool ok);
}  // namespace wire
