#pragma once

#include <stdint.h>

// 時刻の読み取り口 (docs/loop_nonblocking_spec.md §7.2)。時刻が要る部品はこれを参照で受け取る。
// 実装は SystemClock (util/system_clock.hpp)。時刻は起動からの経過で、読んだ瞬間の値を返す。
// どの文脈 (loop・ISR・割り込み禁止中) から呼んでもよい。
class Clock {
   public:
    virtual ~Clock() = default;

    virtual uint64_t nowUs64() const = 0;
    uint32_t nowUs() const { return (uint32_t)nowUs64(); }           // 下位 32 bit (71.6 分で一周)
    uint32_t nowMs() const { return (uint32_t)(nowUs64() / 1000); }  // 下位 32 bit (49.7 日で一周)
};
