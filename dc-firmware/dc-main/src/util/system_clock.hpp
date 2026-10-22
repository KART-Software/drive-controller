#pragma once

#include <Arduino.h>

#include "interfaces/util/clock.hpp"

// Clock の実装 (docs/loop_nonblocking_spec.md §7.2)。こちらのコードは millis() / micros() を使わずこれを使う。
// 状態は持たない (時刻は PIT のハードウェアが数える)。
//
// 時刻源は PIT の Lifetime Timer (チャネル 0 と 1 をつないだ 64 bit のダウンカウンタ、24 MHz。RM §53.4.5)。
// - millis() / micros() は SysTick 割り込みで進むので、flash 書き込みのように割り込みが長く止まると時間を失う。
//   PIT はハードウェアで数え続けるので失わない。64 bit なので一周しない (約 24,000 年)。
// - PIT はソフトリセット (SYSRESETREQ = COLD reset) でもリセットされる (RM Table 21-3 の Functional modules)。
//   コンストラクタで動かした時点が毎回 0 になる。DWT サイクルカウンタはデバッグ回路にあり、電源投入時しか
//   リセットされないので (RM §21.3.4.2.2、ARMv7-M ARM §B1.5.1) 時刻の原点には使えない。
// - チャネル 0 / 1 を占有する。IntervalTimer は TCTRL が 0 の空きチャネルを 0 から探すので、
//   IntervalTimer::begin() (setup の中) より先に動かしていれば 2 以降を使う。
//
// - 実体は main.cpp に 1 つだけ置く (作るたびに PIT を 0 から動かし直す)。static 初期化で作られ、
//   Teensy コアはクロック設定を終えてから C++ のコンストラクタを呼ぶので、PIT にクロックを入れて動かせる。
//   参照を受け取る部品より前に定義すること (同じファイルの中は定義順に作られる)。
// - 時刻が要る部品は Clock の参照を受け取るか、時刻を引数で受け取る。
// - 短い区間の計測や待ちのタイムアウトは StopWatch (util/stop_watch.hpp)。
class SystemClock final : public Clock {
   public:
    SystemClock() {
        CCM_CCGR1 |= CCM_CCGR1_PIT(CCM_CCGR_ON);
        PIT_MCR = 0;  // MDIS = 0: PIT を有効にする
        // チャネル 1: チャネル 0 が一周するたびに 1 減る (chain)。割り込みなし
        PIT_TCTRL1 = 0;
        PIT_LDVAL1 = 0xFFFFFFFF;
        PIT_TCTRL1 = PIT_TCTRL_CHN;
        PIT_TCTRL1 |= PIT_TCTRL_TEN;
        // チャネル 0: PIT クロック (24 MHz) で 1 ずつ減る。割り込みなし
        PIT_TCTRL0 = 0;
        PIT_LDVAL0 = 0xFFFFFFFF;
        PIT_TCTRL0 = PIT_TCTRL_TEN;
    }

    // LTMR64H を読むとその時点の LTMR64L が保持されるので (RM §53.6.4)、H → L の順に読む。
    // 間に別の ISR が H を読むと保持された L が上書きされるので、2 回の読み出しの間だけ割り込みを止める
    uint64_t nowUs64() const override {
        uint32_t primask;
        __asm__ volatile("mrs %0, primask" : "=r"(primask));
        __disable_irq();
        const uint32_t h = PIT_LTMR64H;
        const uint32_t l = PIT_LTMR64L;
        if (primask == 0)
            __enable_irq();
        const uint64_t ticks = ~(((uint64_t)h << 32) | l);  // ダウンカウンタなので反転すると経過カウント数
        return ticks / kTicksPerUs;
    }

   private:
    // PIT のクロックは Teensy コアが 24 MHz の水晶発振器にしている (startup.c、CCM_CSCMR1 PERCLK_CLK_SEL)
    static constexpr uint32_t kTicksPerUs = 24;
};
