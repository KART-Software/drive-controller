#pragma once

#include <Arduino.h>

#include "util/stop_watch.hpp"

// flash (LittleFS) 書き込みの前後で ETC を止めて再開するための受け渡し (docs/loop_nonblocking_spec.md §6.8)。
// flash への書き込み中は割り込みが約 150 ms 禁止され、tick (モーター制御を含む) が止まる。
//   loop (Flash)  : acquire() → 書き込み → release()
//   tick          : requested() を見たら、シフターのパルス中でなければ ETC を止めて grant()
// tick が動き出す前 (setup 中) は待たずに書く。
class FlashWriteGate {
   public:
    // clock: 許可待ちのタイムアウトを測る (tick が止まっていても進む)
    explicit FlashWriteGate(const Clock& clock) : clock_(clock) {}

    // tick が動き出したら呼ぶ (以後の書き込みは tick の許可を待つ)
    void enable() { enabled_ = true; }

    // loop から。tick が ETC を止めるまで待つ。timeoutMs で諦めたら false (書き込まないこと)
    bool acquire(uint32_t timeoutMs) {
        if (!enabled_)
            return true;
        granted_ = false;
        requested_ = true;
        const StopWatch watch(clock_);  // tick が止まっていても進む時計で測る
        while (!granted_) {
            if (watch.elapsedUs() >= timeoutMs * 1000) {
                requested_ = false;
                return false;
            }
        }
        return true;
    }
    void release() {
        requested_ = false;
        granted_ = false;
    }

    // tick から
    bool requested() const { return requested_; }
    void grant() { granted_ = true; }

   private:
    const Clock& clock_;
    volatile bool enabled_ = false;
    volatile bool requested_ = false;
    volatile bool granted_ = false;
};
