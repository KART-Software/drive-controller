#include "gpt_timer.hpp"

namespace {

void (*userIsr)() = nullptr;

void gpt2Isr() {
    GPT2_SR = GPT_SR_OF1;  // 先にフラグを下ろす (ISR 末尾で下ろすと、書き込みが届く前に再入することがある)
    userIsr();
    asm volatile("dsb");
}

}  // namespace

void Gpt2Timer::begin(uint32_t periodUs, void (*isr)(), uint8_t priority) {
    userIsr = isr;
    CCM_CCGR0 |= CCM_CCGR0_GPT2_BUS(CCM_CCGR_ON) | CCM_CCGR0_GPT2_SERIAL(CCM_CCGR_ON);
    GPT2_CR = 0;
    GPT2_PR = 0;                     // 分周なし (24 MHz)
    GPT2_OCR1 = periodUs * 24 - 1;   // 比較一致でカウンタは 0 に戻る (FRR=0 の restart モード)
    GPT2_SR = 0x3F;
    GPT2_IR = GPT_IR_OF1IE;
    attachInterruptVector(IRQ_GPT2, gpt2Isr);
    NVIC_SET_PRIORITY(IRQ_GPT2, priority);
    NVIC_ENABLE_IRQ(IRQ_GPT2);
    GPT2_CR = GPT_CR_EN | GPT_CR_CLKSRC(1);  // 周辺クロック (Teensy は 24 MHz の水晶に設定済み)
}

void Gpt2Timer::end() {
    NVIC_DISABLE_IRQ(IRQ_GPT2);
    GPT2_CR = 0;
}
