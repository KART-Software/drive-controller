#pragma once

#include <Arduino.h>

// DWT サイクルカウンタから作る µs 時刻 (docs/loop_nonblocking_spec.md §7.2)。
// micros() / millis() は SysTick 割り込みでミリ秒を足すので、flash 書き込みのように割り込みが長く止まると
// その時間を失う。サイクルカウンタはハードウェアで数え続けるので、7.16 s (600 MHz) で一周する前に一度読めば
// 時間を失わない。
//
// update() は tickISR の 1 か所だけから呼ぶ (64 bit に伸ばす状態を持つので、複数の文脈から呼ぶと壊れる)。
// 他の文脈は nowUs() で、直近の update() の値を読む。
class Clock {
   public:
    uint64_t update() {
        const uint32_t c = ARM_DWT_CYCCNT;
        if (c < lastCycles_)
            high_ += (1ull << 32);
        lastCycles_ = c;
        const uint64_t us = (high_ | c) / (F_CPU_ACTUAL / 1000000);
        nowUs_ = (uint32_t)us;
        return us;
    }
    uint32_t nowUs() const { return nowUs_; }  // 下位 32 bit (71.6 分で一周)

   private:
    uint32_t lastCycles_ = 0;
    uint64_t high_ = 0;
    volatile uint32_t nowUs_ = 0;
};
