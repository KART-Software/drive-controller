#pragma once

#include <stdint.h>

#include "interfaces/util/clock.hpp"

// 短い区間の計測と待ちのタイムアウト。作った時点 (または restart / lapUs) からの経過を Clock で測る。
// Clock::nowUs64() は tick が止まっていても進むので、「tick が止まったら諦める」待ちにも使える。
// elapsedUs() / lapUs() は 32 bit (71.6 分) までの区間を返す。
// 1 つの StopWatch を複数の文脈 (ISR と loop など) で共有しないこと。
class StopWatch {
   public:
    explicit StopWatch(const Clock& clock) : clock_(clock), startUs_(clock.nowUs64()) {}

    uint32_t elapsedUs() const { return (uint32_t)(clock_.nowUs64() - startUs_); }
    void restart() { startUs_ = clock_.nowUs64(); }
    // 前回からの経過を返し、起点を今に進める (周期の計測用)
    uint32_t lapUs() {
        const uint64_t now = clock_.nowUs64();
        const uint32_t us = (uint32_t)(now - startUs_);
        startUs_ = now;
        return us;
    }

   private:
    const Clock& clock_;
    uint64_t startUs_;
};
