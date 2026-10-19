#include "quadtimer_edge_source.hpp"

namespace {

// 一周 (65536 × 0.43 µs ≈ 28 ms) に近づく前に対応を見失ったとみなす
constexpr uint64_t kMaxPollGapUs = 20000;

constexpr uint16_t kPcsIpBusDiv64 = 0xE;  // CTRL[PCS] 1110b = IP bus clock ÷64

}  // namespace

bool QuadTimerEdgeSource::begin(bool bothEdges) {
    // ピン → QuadTimer3 のチャネル、ALT、入力選択 (daisy)。リセット値の daisy は別のパッドを指すので必ず設定する
    switch (pin_) {
        case 14:  // GPIO_AD_B1_02 ALT1 = QTIMER3_TIMER2
            countCh_ = 2;
            capCh_ = 0;
            IOMUXC_QTIMER3_TIMER2_SELECT_INPUT = 1;  // 01 = GPIO_AD_B1_02_ALT1
            break;
        case 15:  // GPIO_AD_B1_03 ALT1 = QTIMER3_TIMER3
            countCh_ = 3;
            capCh_ = 1;
            IOMUXC_QTIMER3_TIMER3_SELECT_INPUT = 1;  // 01 = GPIO_AD_B1_03_ALT1
            break;
        default:
            enabled_ = false;
            return false;
    }
    timer_ = &IMXRT_TMR3;
    CCM_CCGR6 |= CCM_CCGR6_QTIMER3(CCM_CCGR_ON);

    pinMode(pin_, INPUT_PULLUP);
    *(portConfigRegister(pin_)) = 1;  // ALT1

    configure(bothEdges);
    timer_->ENBL |= (1 << countCh_) | (1 << capCh_);
    enabled_ = true;
    return true;
}

void QuadTimerEdgeSource::configure(bool bothEdges) {
    IMXRT_TMR_CH_t& cnt = timer_->CH[countCh_];
    cnt.CTRL = 0;
    cnt.SCTRL = 0;
    cnt.CSCTRL = 0;
    cnt.LOAD = 0;
    cnt.COMP1 = 0xFFFF;
    cnt.CMPLD1 = 0xFFFF;
    cnt.CNTR = 0;
    // 主入力 = 自分の入力ピン。CM 1 = 立ち上がり、2 = 立ち上がりと立ち下がり
    cnt.CTRL = TMR_CTRL_CM(bothEdges ? 2 : 1) | TMR_CTRL_PCS(countCh_);

    IMXRT_TMR_CH_t& cap = timer_->CH[capCh_];
    cap.CTRL = 0;
    cap.SCTRL = 0;
    cap.CSCTRL = 0;
    cap.LOAD = 0;
    cap.COMP1 = 0xFFFF;
    cap.CMPLD1 = 0xFFFF;
    cap.CNTR = 0;
    // 主入力 = IP バスクロック ÷64 を数えて自走。副入力 (SCS) = 数えるチャネルの入力ピンで CAPT に取る
    cap.SCTRL = TMR_SCTRL_CAPTURE_MODE(bothEdges ? 3 : 1);
    cap.CTRL = TMR_CTRL_CM(1) | TMR_CTRL_PCS(kPcsIpBusDiv64) | TMR_CTRL_SCS(countCh_);

    lastCountRaw_ = cnt.CNTR;
    anchored_ = false;
    armed_ = false;
    lostPending_ = true;
}

void QuadTimerEdgeSource::setBothEdges(bool bothEdges) {
    if (enabled_)
        configure(bothEdges);
}

uint32_t QuadTimerEdgeSource::readCount() {
    const uint16_t raw = timer_->CH[countCh_].CNTR;
    count_ += (uint16_t)(raw - lastCountRaw_);
    lastCountRaw_ = raw;
    return count_;
}

EdgeSnapshot QuadTimerEdgeSource::poll(uint64_t nowUs) {
    EdgeSnapshot s;
    if (!enabled_)
        return s;
    IMXRT_TMR_CH_t& cap = timer_->CH[capCh_];

    // 1) 前回 IEF を消した後の最初のエッジ (CAPT は CNTR を読む前に読む: 読んだ後のエッジで CAPT > CNTR にならないように)
    const bool captured = armed_ && (cap.SCTRL & TMR_SCTRL_IEF);
    const uint16_t capt = cap.CAPT;
    const uint16_t capNow = cap.CNTR;

    // 2) キャプチャ側の時間軸を伸ばす。間が空きすぎたら対応を取り直す
    if (anchored_ && nowUs - lastPollUs_ < kMaxPollGapUs) {
        cap64_ += (uint16_t)(capNow - lastCapRaw_);
    } else {
        if (anchored_)
            s.lost = true;
        cap64_ = 0;
        offsetUs_ = (double)nowUs;  // 時間軸の 0 = 今の tick の時刻
        anchored_ = true;
        armed_ = false;
    }
    lastCapRaw_ = capNow;
    lastPollUs_ = nowUs;

    if (captured && !s.lost) {
        const uint64_t edge64 = cap64_ - (uint16_t)(capNow - capt);
        s.hasEdge = true;
        s.edgeIndex = countAtClear_ + 1;
        s.edgeUs = (double)edge64 * tickUs() + offsetUs_;
    }

    // 3) IEF を消し、その時点のエッジ数を覚える (消す途中にエッジが来たらやり直す)
    uint32_t c2;
    for (;;) {
        const uint32_t c1 = readCount();
        cap.SCTRL &= ~TMR_SCTRL_IEF;
        c2 = readCount();
        if (c1 == c2)
            break;
    }
    countAtClear_ = c2;
    armed_ = true;
    s.count = c2;

    if (lostPending_) {
        s.lost = true;
        s.hasEdge = false;
        lostPending_ = false;
    }
    return s;
}
