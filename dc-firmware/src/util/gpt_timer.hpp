#pragma once

#include <Arduino.h>

// GPT2 による周期割り込み (docs/loop_nonblocking_spec.md §6.1)。
// IntervalTimer (PIT) は 4 チャネルが IRQ_PIT を共有し、優先度を個別に持てない。GPT は IRQ を
// 1 本ずつ持つので、ここで決めた優先度がそのまま効く。クロックは 24 MHz の周辺クロックで、
// 周期は整数カウント (1 µs = 24 カウント) なので誤差は出ない。
class Gpt2Timer {
   public:
    void begin(uint32_t periodUs, void (*isr)(), uint8_t priority);
    void end();
};
