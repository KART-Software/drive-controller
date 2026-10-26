#pragma once

#include <stdint.h>

// CAN のフレーム (docs/can_bus_spec.md §3)。kart-can の pack / unpack と同じ「ID + 長さ + バイト列」。標準 ID (11 bit) のデータフレーム。
// CAN FD に備えてデータは 64 B まで持てる (classic では len ≤ 8、fd / brs は false。FD の有効化は未対応、§4.8)
struct CanFrame {
    uint32_t id = 0;  // 標準 ID のみ
    uint8_t len = 0;
    bool fd = false;
    bool brs = false;  // FD のデータ相で速度を上げる (Bit Rate Switch)
    uint8_t data[64] = {};
};
