#include "flexcan3_bus.hpp"

#include <Arduino.h>

#include "constants.hpp"

// ピンは実配線と照合するために constants.hpp に置く。このドライバが設定するパッドは pin 30 (GPIO_EMC_37) / pin 31 (GPIO_EMC_36)
static_assert(CAN3_RX_PIN == 30 && CAN3_TX_PIN == 31, "CAN3 は Teensy 4.1 の pin 30 (CRX3) / 31 (CTX3) に固定");

namespace {

// ── レジスタ (RM Chapter 45 のオフセット。ベース 0x401D8000) ──
constexpr uint32_t kBase = IMXRT_FLEXCAN3_ADDRESS;
volatile uint32_t& reg(uint32_t offset) { return *reinterpret_cast<volatile uint32_t*>(kBase + offset); }
constexpr uint32_t MCR = 0x00, CTRL1 = 0x04, TIMER = 0x08, RXMGMASK = 0x10, ECR = 0x1C, ESR1 = 0x20, IMASK1 = 0x28,
                   IFLAG1 = 0x30, CTRL2 = 0x34, RXFGMASK = 0x48;
// メールボックス: 0x80 から kMbBytes ごと。classic (データ 8 B) は 16 B (CS / ID / WORD0 / WORD1)。
// CAN FD でデータを 64 B にすると 72 B になり、使えるメールボックスの数も減る (docs/can_bus_spec.md §4.8)
constexpr uint32_t kMbBytes = 16;
uint32_t mbWord(uint8_t n, uint8_t word) { return 0x80 + n * kMbBytes + word * 4; }
uint32_t rximr(uint8_t n) { return 0x880 + n * 4; }  // 個別マスク

// MCR
constexpr uint32_t MCR_MDIS = 1u << 31, MCR_FRZ = 1u << 30, MCR_RFEN = 1u << 29, MCR_HALT = 1u << 28,
                   MCR_SOFTRST = 1u << 25, MCR_FRZACK = 1u << 24, MCR_WRNEN = 1u << 21, MCR_LPMACK = 1u << 20,
                   MCR_SRXDIS = 1u << 17, MCR_IRMQ = 1u << 16, MCR_DMA = 1u << 15, MCR_FDEN = 1u << 11,
                   MCR_IDAM_MASK = 3u << 8, MCR_MAXMB_MASK = 0x7F;
// CTRL1 (ビットタイミング以外のビットは 0 = BOFFREC 自動復帰、エラー割り込みなし、CLKSRC = 発振器)
constexpr uint32_t CTRL1_PRESDIV(uint32_t v) { return v << 24; }
constexpr uint32_t CTRL1_RJW(uint32_t v) { return v << 22; }
constexpr uint32_t CTRL1_PSEG1(uint32_t v) { return v << 19; }
constexpr uint32_t CTRL1_PSEG2(uint32_t v) { return v << 16; }
constexpr uint32_t CTRL1_PROPSEG(uint32_t v) { return v; }
// 1 Mbit/s: 24 MHz ÷ (1 + 1) = 12 MHz → 1 bit = 1 + 3 + 5 + 3 = 12 Tq、サンプル点 9 / 12 = 75 % (docs/can_bus_spec.md §4.2)
constexpr uint32_t kCtrl1Timing1M = CTRL1_PRESDIV(1) | CTRL1_RJW(1) | CTRL1_PSEG1(4) | CTRL1_PSEG2(2) | CTRL1_PROPSEG(2);
// CTRL2
constexpr uint32_t CTRL2_EACEN = 1u << 16;  // マスクの bit 31 / 30 で RTR / IDE も比較する
// ESR1
constexpr uint32_t ESR1_FLTCONF_SHIFT = 4, ESR1_SYNCH = 1u << 18;
constexpr uint32_t ESR1_W1C = 0x003B0007;  // WAKINT ERRINT BOFFINT / RWRNINT TWRNINT / BOFFDONEINT ERRINT_FAST ERROVR
// メールボックスの CS
constexpr uint32_t CS_CODE_SHIFT = 24, CS_DLC_SHIFT = 16;
constexpr uint32_t CODE_RX_EMPTY = 0x4, CODE_RX_FULL = 0x2, CODE_RX_OVERRUN = 0x6, CODE_BUSY_BIT = 0x1;
constexpr uint32_t CODE_TX_INACTIVE = 0x8, CODE_TX_ONCE = 0xC;
constexpr uint32_t ID_STD_SHIFT = 18;         // MB の ID ワード (bit 28〜18)
constexpr uint32_t RX_MASK_STD = 0xDFFC0000;  // RTR (31) / IDE (30) / 標準 ID (28〜18) を全部比較する

constexpr uint8_t kFirstTxMb = 8, kLastTxMb = 23;  // MB 0〜7 は受信 (フィルタの数だけ使う)、8〜23 は送信 (16 本。1 tick に最大 6 + 1 フレーム)
static_assert(kFirstTxMb == CanRxFilters::kMax, "受信メールボックスの数 = フィルタの最大数");
constexpr int kBusyRetries = 100;  // 受信メールボックスの BUSY (書き込み中、数十クロック) を待つ上限
constexpr uint32_t kPadCtl = IOMUXC_PAD_PKE | IOMUXC_PAD_SPEED(2) | IOMUXC_PAD_DSE(6);  // 0x10B0
constexpr uint32_t kMuxAlt9Sion = 0x19;

Flexcan3Bus* instance = nullptr;  // ISR から自分に届くため (単一インスタンス)

void enterFreeze() {
    reg(MCR) |= MCR_FRZ | MCR_HALT;
    while (!(reg(MCR) & MCR_FRZACK)) {
    }
}

void exitFreeze() {
    reg(MCR) &= ~MCR_HALT;  // FRZ は立てたまま (デバッグ時の停止用)
    while (reg(MCR) & MCR_FRZACK) {
    }
}

}  // namespace

bool Flexcan3Bus::begin(const CanConfig& config, const CanRxFilters& filters, RxCallback onRx, uint8_t irqPriority) {
    if (config.fd || config.bitrate != 1000000 || filters.count > CanRxFilters::kMax)
        return false;  // FD は未対応 (§4.8)、ビットタイミングの表は 1 Mbit/s だけ
    onRx_ = onRx;
    instance = this;
    rxMbCount_ = (uint8_t)filters.count;
    rxMbMask_ = (1u << rxMbCount_) - 1;

    // クロック: CAN_CLK_ROOT = 24 MHz の水晶 (CSCMR2 CAN_CLK_SEL = 1、PODF = 0)、CAN3 のゲートを開く
    CCM_CSCMR2 = (CCM_CSCMR2 & ~(CCM_CSCMR2_CAN_CLK_SEL(3) | CCM_CSCMR2_CAN_CLK_PODF(0x3F))) |
                 CCM_CSCMR2_CAN_CLK_SEL(1) | CCM_CSCMR2_CAN_CLK_PODF(0);
    CCM_CCGR7 |= CCM_CCGR7_CAN3(CCM_CCGR_ON) | CCM_CCGR7_CAN3_SERIAL(CCM_CCGR_ON);

    // ピン: pin 31 = GPIO_EMC_36 (CTX3)、pin 30 = GPIO_EMC_37 (CRX3)。ALT9、入力選択 (daisy) は EMC_37
    IOMUXC_SW_MUX_CTL_PAD_GPIO_EMC_36 = kMuxAlt9Sion;
    IOMUXC_SW_PAD_CTL_PAD_GPIO_EMC_36 = kPadCtl;
    IOMUXC_SW_MUX_CTL_PAD_GPIO_EMC_37 = kMuxAlt9Sion;
    IOMUXC_SW_PAD_CTL_PAD_GPIO_EMC_37 = kPadCtl;
    IOMUXC_CANFD_IPP_IND_CANRX_SELECT_INPUT = 0;

    // モジュールを有効にして freeze、ソフトリセット (RM §45.5.1: 設定は freeze モードで行う)
    reg(MCR) &= ~MCR_MDIS;
    while (reg(MCR) & MCR_LPMACK) {
    }
    enterFreeze();
    reg(MCR) |= MCR_SOFTRST;
    while (reg(MCR) & MCR_SOFTRST) {
    }
    while (!(reg(MCR) & MCR_FRZACK)) {
    }

    // MCR: 自己受信なし、個別マスク、警告フラグ、FIFO / DMA / FD なし、MB 0〜23
    uint32_t mcr = reg(MCR);
    mcr |= MCR_SRXDIS | MCR_IRMQ | MCR_WRNEN;
    mcr &= ~(MCR_RFEN | MCR_DMA | MCR_FDEN | MCR_IDAM_MASK | MCR_MAXMB_MASK);
    mcr |= kLastTxMb;  // MAXMB = 23
    reg(MCR) = mcr;
    reg(CTRL1) = kCtrl1Timing1M;
    reg(CTRL2) |= CTRL2_EACEN;

    // メールボックスと個別マスクを全部クリア。(FIFO を使う場合は RFEN を立てる前に行うこと。立てた後に FIFO の予約領域
    // 0x90〜0xDC へ書くと CPU が止まる)
    reg(IMASK1) = 0;
    for (uint8_t n = 0; n <= kLastTxMb; n++) {
        for (uint8_t w = 0; w < 4; w++)
            reg(mbWord(n, w)) = 0;
        reg(rximr(n)) = 0;
    }
    reg(RXMGMASK) = RX_MASK_STD;
    reg(RXFGMASK) = RX_MASK_STD;
    reg(IFLAG1) = reg(IFLAG1);

    // 受信メールボックス: フィルタの ID ごとに 1 本。標準 ID のデータフレームだけ一致させる
    for (uint8_t n = 0; n < rxMbCount_; n++) {
        reg(mbWord(n, 1)) = (filters.ids[n] & 0x7FF) << ID_STD_SHIFT;
        reg(rximr(n)) = RX_MASK_STD;
        reg(mbWord(n, 0)) = CODE_RX_EMPTY << CS_CODE_SHIFT;
    }
    // 送信メールボックス 8〜15
    for (uint8_t n = kFirstTxMb; n <= kLastTxMb; n++)
        reg(mbWord(n, 0)) = CODE_TX_INACTIVE << CS_CODE_SHIFT;

    // 割り込みは受信メールボックスだけ
    reg(IMASK1) = rxMbMask_;
    attachInterruptVector(IRQ_CAN3, isr);
    NVIC_SET_PRIORITY(IRQ_CAN3, irqPriority);
    NVIC_ENABLE_IRQ(IRQ_CAN3);

    exitFreeze();  // 以後 11 bit の recessive を待ってバスに同期する
    return true;
}

bool Flexcan3Bus::send(const CanFrame& frame) {
    if (frame.fd || frame.len > 8) {  // classic だけ (§4.8)。黙って丸めずに捨てる
        txDropped_ = txDropped_ + 1;
        return false;
    }
    for (uint8_t n = kFirstTxMb; n <= kLastTxMb; n++) {
        if (((reg(mbWord(n, 0)) >> CS_CODE_SHIFT) & 0xF) != CODE_TX_INACTIVE)
            continue;  // 送信中 (TX_ONCE) か未完了
        reg(IFLAG1) = 1u << n;  // 前回の完了フラグ (割り込みは出さない設定)
        reg(mbWord(n, 0)) = CODE_TX_INACTIVE << CS_CODE_SHIFT;
        reg(mbWord(n, 1)) = (frame.id & 0x7FF) << ID_STD_SHIFT;
        reg(mbWord(n, 2)) = ((uint32_t)frame.data[0] << 24) | ((uint32_t)frame.data[1] << 16) |
                            ((uint32_t)frame.data[2] << 8) | frame.data[3];
        reg(mbWord(n, 3)) = ((uint32_t)frame.data[4] << 24) | ((uint32_t)frame.data[5] << 16) |
                            ((uint32_t)frame.data[6] << 8) | frame.data[7];
        reg(mbWord(n, 0)) = (CODE_TX_ONCE << CS_CODE_SHIFT) | ((uint32_t)frame.len << CS_DLC_SHIFT);
        return true;
    }
    txDropped_ = txDropped_ + 1;
    return false;
}

void Flexcan3Bus::isr() {
    instance->handleInterrupt();
}

void Flexcan3Bus::handleInterrupt() {
    isrCount_ = isrCount_ + 1;
    const uint32_t iflag = reg(IFLAG1) & rxMbMask_;
    for (uint8_t n = 0; n < rxMbCount_; n++) {
        const uint32_t bit = 1u << n;
        if (!(iflag & bit))
            continue;
        // CS を読むとメールボックスがロックされる (読んでいる間は次のフレームで上書きされない)。
        // BUSY (書き込み中、数十クロック) なら読み直す
        uint32_t cs = reg(mbWord(n, 0));
        for (int i = 0; (cs >> CS_CODE_SHIFT) & CODE_BUSY_BIT; i++) {
            if (i >= kBusyRetries)
                break;
            cs = reg(mbWord(n, 0));
        }
        const uint32_t code = (cs >> CS_CODE_SHIFT) & 0xF;
        if (code & CODE_BUSY_BIT)
            continue;  // まだ書き込み中。フラグは残るので、戻ったあと再び割り込んで読み直す
        if (code != CODE_RX_FULL && code != CODE_RX_OVERRUN) {
            reg(IFLAG1) = bit;  // 想定外のコード: フラグだけ消す
            continue;
        }
        const uint32_t id = reg(mbWord(n, 1));
        const uint32_t w0 = reg(mbWord(n, 2));
        const uint32_t w1 = reg(mbWord(n, 3));
        // フラグは**ロックを解除する前に**消す。解除の瞬間にロック中に届いた次のフレームが移ってきてフラグが立つので、
        // 解除後に消すとそのフラグを消してしまい、次が来るまで読まれないフレームが残る
        reg(IFLAG1) = bit;  // このビットだけ消す (読んで書き戻すと、入ってから立った別のフラグを消してしまう)
        reg(mbWord(n, 0)) = CODE_RX_EMPTY << CS_CODE_SHIFT;  // 次のフレームを受けられるようにする
        const uint32_t unlock = reg(TIMER);                   // ロックを解除 (読むだけ)
        (void)unlock;
        if (code == CODE_RX_OVERRUN)
            rxOverrun_ = rxOverrun_ + 1;  // 読む前に次が来て、前のフレームを失った
        CanFrame frame;
        frame.id = (id >> ID_STD_SHIFT) & 0x7FF;
        const uint32_t dlc = (cs >> CS_DLC_SHIFT) & 0xF;
        frame.len = (uint8_t)(dlc > 8 ? 8 : dlc);
        frame.data[0] = (uint8_t)(w0 >> 24);
        frame.data[1] = (uint8_t)(w0 >> 16);
        frame.data[2] = (uint8_t)(w0 >> 8);
        frame.data[3] = (uint8_t)w0;
        frame.data[4] = (uint8_t)(w1 >> 24);
        frame.data[5] = (uint8_t)(w1 >> 16);
        frame.data[6] = (uint8_t)(w1 >> 8);
        frame.data[7] = (uint8_t)w1;
        rxFrames_ = rxFrames_ + 1;
        if (onRx_)
            onRx_(frame);
    }
    // エラー系のフラグ (w1c) はここで消す。割り込みの要因にはしていないが、立ったままにしない
    reg(ESR1) = reg(ESR1) & ESR1_W1C;
    asm volatile("dsb");  // フラグのクリアが届く前に戻って再入しないように
}

CanState Flexcan3Bus::state() const {
    const uint32_t esr1 = reg(ESR1);
    const uint32_t ecr = reg(ECR);
    const uint32_t flt = (esr1 >> ESR1_FLTCONF_SHIFT) & 0x3;  // 00 active / 01 passive / 1x bus-off
    return {
        .fault = flt == 0 ? CanState::Fault::Active : flt == 1 ? CanState::Fault::Passive : CanState::Fault::BusOff,
        .txErrors = (uint8_t)(ecr & 0xFF),
        .rxErrors = (uint8_t)((ecr >> 8) & 0xFF),
        .synced = (esr1 & ESR1_SYNCH) != 0,
    };
}

CanStats Flexcan3Bus::stats() const {
    return {.rxFrames = rxFrames_, .rxOverrun = rxOverrun_, .txDropped = txDropped_, .isrCount = isrCount_};
}
