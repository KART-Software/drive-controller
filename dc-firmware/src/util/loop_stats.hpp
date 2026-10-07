#pragma once

#include <Arduino.h>

// loop() / SD / 安全層 ISR の所要時間を 1 s 窓で集計する (docs/loop_nonblocking_spec.md §5)。
//   tick(micros())  : loop() 先頭で毎回。前回からの経過 = 1 周の所要として max/mean を更新
//   noteSdUs()      : SD write/flush 1 回の所要 (SensorLogger が呼び出しごとに直接報告)
//   noteSafetyUs()  : 安全層 ISR の所要 (Phase 2 以降。ISR から呼ぶので volatile)
//   rollover()      : 1 s ごとに loop 先頭 (tick の直後) から。窓を確定して直前の窓として保持し初期化。
//                     窓境界を loop 先頭に置くことで、同じ周の loop 所要と SD 所要が同じ窓に入る
// live() (直前の窓と現在の窓のここまでの大きい方) を telemetry (SysStats) と SD ログ (LogRecord v4) に載せる。
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
        // curSafetyMax_ は安全層 ISR が書くので、読み出しとゼロ化の間に割り込まれると 1 サンプル消える
        noInterrupts();
        uint32_t safetyMax = curSafetyMax_;
        curSafetyMax_ = 0;
        interrupts();
        last_.loopMaxUs = curLoopMax_;
        last_.loopMeanUs = curLoopN_ ? (uint32_t)(curLoopSum_ / curLoopN_) : 0;
        last_.sdMaxUs = curSdMax_;
        last_.safetyMaxUs = safetyMax;
        last_.logDrops = logDrops;
        last_.loopMaxUsBoot = bootLoopMax_;
        curLoopMax_ = 0;
        curLoopSum_ = 0;
        curLoopN_ = 0;
        curSdMax_ = 0;
    }
    // telemetry と SD ログが読む値: 直前に完了した窓と「現在の窓のここまで」の大きい方 (mean は現在の窓に
    // 1 周以上あればその平均、無ければ直前の窓)。
    // - 直前の窓だけだと、停止の値が 1〜2 s 後のフレーム / レコードにしか載らず、SD ログの t_ms の穴と
    //   突き合わせられない。tick() は loop 先頭なので、停止した周の直後の周から値が上がる
    // - 起動直後 (まだ窓が 1 つも完了していない) でも 0 ではなく実測値が出る
    Snapshot live() const {
        Snapshot s = last_;
        if (curLoopMax_ > s.loopMaxUs)
            s.loopMaxUs = curLoopMax_;
        if (curLoopN_ > 0)
            s.loopMeanUs = (uint32_t)(curLoopSum_ / curLoopN_);
        if (curSdMax_ > s.sdMaxUs)
            s.sdMaxUs = curSdMax_;
        uint32_t safety = curSafetyMax_;
        if (safety > s.safetyMaxUs)
            s.safetyMaxUs = safety;
        if (bootLoopMax_ > s.loopMaxUsBoot)
            s.loopMaxUsBoot = bootLoopMax_;
        return s;
    }

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
