#pragma once

#include <Arduino.h>

class PulseCounter
{
public:
    PulseCounter(uint8_t pin);
    void begin();
    void update(); // Call periodically from main loop to compute frequency
    float getFrequencyHz() const;
    float getRpm(uint8_t pulsesPerRev);
    // 最後の update() 時点のハードウェアカウンタ値 (16bit ラップあり)
    uint32_t count() const { return lastCount; }

private:
    struct TimerInput
    {
        IMXRT_TMR_t *timer;
        uint8_t channel;
        uint8_t mux;
    };

    uint8_t pin;
    IMXRT_TMR_t *timer = nullptr;
    uint8_t channel = 0;
    uint32_t lastCount = 0;
    uint32_t lastUpdateUs = 0;
    float frequencyHz = 0;
    bool enabled = false;

    static bool getTimerInput(uint8_t pin, TimerInput &input);
    static void enableClock(IMXRT_TMR_t *timer);
    uint32_t readCounter() const;
};

