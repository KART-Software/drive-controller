#pragma once

#include <Arduino.h>

// loop() / SD / 安全層 ISR の所要時間を 1 s 窓で集計する (docs/loop_nonblocking_spec.md §5)。
//   tick(micros())  : loop() 先頭で毎回。前回からの経過 = 1 周の所要として max/mean を更新
//   noteSdUs()      : SD write/flush の所要 (SensorLogger から 1 s ごとにまとめて報告)
//   noteSafetyUs()  : 安全層 ISR の所要 (Phase 2 以降。ISR から呼ぶので volatile)
//   rollover()      : 1 s ごとに loop から。窓を確定して last() に移し、カウンタを初期化
// last() は「直近に完了した 1 s 窓」の値で、telemetry (SysStats) と SD ログ (LogRecord v4) に載せる。
class LoopStats {
   public:
    struct Snapshot {
        uint32_t loopMaxUs = 0;      // 窓内の loop 1 周の最大
        uint32_t loopMeanUs = 0;     // 窓内の平均
        uint32_t sdMaxUs = 0;        // 窓内の SD write/flush 1 回の最大
        uint32_t safetyMaxUs = 0;    // 窓内の安全層 ISR 1 回の最大 (Phase 2 以降、それまで 0)
        uint32_t logDrops = 0;       // リング溢れ累積 (Phase 3 以降、それまで 0)
        uint32_t loopMaxUsBoot = 0;  // 起動以来の loop 1 周の最大 (窓でリセットしない)
    };

    void tick(uint32_t nowUs) {
        if (havePrev_) {
            uint32_t dt = nowUs - prevUs_;
            if (dt > curLoopMax_)
                curLoopMax_ = dt;
            if (dt > bootLoopMax_)
                bootLoopMax_ = dt;
            curLoopSum_ += dt;
            curLoopN_++;
        }
        prevUs_ = nowUs;
        havePrev_ = true;
    }
    void noteSdUs(uint32_t us) {
        if (us > curSdMax_)
            curSdMax_ = us;
    }
    void noteSafetyUs(uint32_t us) {
        if (us > curSafetyMax_)
            curSafetyMax_ = us;
    }
    void rollover(uint32_t logDrops) {
        last_.loopMaxUs = curLoopMax_;
        last_.loopMeanUs = curLoopN_ ? (uint32_t)(curLoopSum_ / curLoopN_) : 0;
        last_.sdMaxUs = curSdMax_;
        last_.safetyMaxUs = curSafetyMax_;
        last_.logDrops = logDrops;
        last_.loopMaxUsBoot = bootLoopMax_;
        curLoopMax_ = 0;
        curLoopSum_ = 0;
        curLoopN_ = 0;
        curSdMax_ = 0;
        curSafetyMax_ = 0;
    }
    const Snapshot& last() const { return last_; }

   private:
    Snapshot last_;
    uint32_t prevUs_ = 0;
    bool havePrev_ = false;
    uint32_t curLoopMax_ = 0;
    uint64_t curLoopSum_ = 0;
    uint32_t curLoopN_ = 0;
    uint32_t bootLoopMax_ = 0;
    uint32_t curSdMax_ = 0;
    volatile uint32_t curSafetyMax_ = 0;
};
