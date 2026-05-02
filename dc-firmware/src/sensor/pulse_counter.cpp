#include "pulse_counter.hpp"

PulseCounter::PulseCounter(uint8_t pin) : pin(pin) {}

void PulseCounter::begin() {
    TimerInput input;
    enabled = getTimerInput(pin, input);
    if (!enabled) {
        frequencyHz = 0;
        return;
    }

    timer = input.timer;
    channel = input.channel;
    enableClock(timer);

    IMXRT_TMR_CH_t& ch = timer->CH[channel];
    ch.CTRL = 0;
    ch.SCTRL = 0;
    ch.CSCTRL = 0;
    ch.LOAD = 0;
    ch.COMP1 = 0xFFFF;
    ch.CMPLD1 = 0xFFFF;
    ch.CNTR = 0;

    pinMode(pin, INPUT_PULLUP);
    *(portConfigRegister(pin)) = input.mux;

    // Count rising edges on this QuadTimer channel's external input.
    ch.CTRL = TMR_CTRL_CM(1) | TMR_CTRL_PCS(channel);
    timer->ENBL |= 1 << channel;

    lastCount = readCounter();
    lastUpdateUs = micros();
}

void PulseCounter::update() {
    if (!enabled) {
        frequencyHz = 0;
        return;
    }

    uint32_t currentCount = readCounter();
    uint32_t now = micros();
    uint32_t elapsed = now - lastUpdateUs;
    if (elapsed > 0) {
        uint32_t delta = (currentCount - lastCount) & 0xFFFF;
        frequencyHz = (float)delta * 1000000.0f / (float)elapsed;
    }
    lastCount = currentCount;
    lastUpdateUs = now;
}

float PulseCounter::getFrequencyHz() const {
    return frequencyHz;
}

float PulseCounter::getRpm(uint8_t pulsesPerRev) {
    if (pulsesPerRev == 0)
        return 0;
    return frequencyHz * 60.0f / pulsesPerRev;
}

bool PulseCounter::getTimerInput(uint8_t pin, TimerInput& input) {
    switch (pin) {
        case 10:
            input = {&IMXRT_TMR1, 0, 1};
            return true;
        case 11:
            input = {&IMXRT_TMR1, 2, 1};
            return true;
        case 12:
            input = {&IMXRT_TMR1, 1, 1};
            return true;
        case 13:
            input = {&IMXRT_TMR2, 0, 1};
            return true;
        case 14:
            input = {&IMXRT_TMR3, 2, 1};
            return true;
        case 15:
            input = {&IMXRT_TMR3, 3, 1};
            return true;
        case 18:
            input = {&IMXRT_TMR3, 1, 1};
            return true;
        case 19:
            input = {&IMXRT_TMR3, 0, 1};
            return true;
#if defined(ARDUINO_TEENSY_MICROMOD)
        case 40:
            input = {&IMXRT_TMR2, 1, 1};
            return true;
        case 41:
            input = {&IMXRT_TMR2, 2, 1};
            return true;
        case 45:
            input = {&IMXRT_TMR4, 0, 1};
            return true;
#endif
        default:
            return false;
    }
}

void PulseCounter::enableClock(IMXRT_TMR_t* timer) {
    if (timer == &IMXRT_TMR1) {
        CCM_CCGR6 |= CCM_CCGR6_QTIMER1(CCM_CCGR_ON);
    } else if (timer == &IMXRT_TMR2) {
        CCM_CCGR6 |= CCM_CCGR6_QTIMER2(CCM_CCGR_ON);
    } else if (timer == &IMXRT_TMR3) {
        CCM_CCGR6 |= CCM_CCGR6_QTIMER3(CCM_CCGR_ON);
    } else if (timer == &IMXRT_TMR4) {
        CCM_CCGR6 |= CCM_CCGR6_QTIMER4(CCM_CCGR_ON);
    }
}

uint32_t PulseCounter::readCounter() const {
    return timer->CH[channel].CNTR;
}
