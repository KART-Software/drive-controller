#pragma once

#include <Arduino.h>

/**
 * Measures the call frequency by sampling `micros()` on each `tick()`.
 * The interval is smoothed with an exponential moving average (EMA):
 *   interval = (interval * (2^SHIFT - 1) + delta) >> SHIFT
 * SHIFT = 3 -> time constant ~8 samples.
 */
class FrequencyMeter
{
public:
    void tick()
    {
        uint32_t now = micros();
        if (lastUs_)
        {
            uint32_t delta = now - lastUs_;
            constexpr uint32_t mask = (1u << SHIFT) - 1u;
            intervalUs_ = intervalUs_ ? (intervalUs_ * mask + delta) >> SHIFT : delta;
        }
        lastUs_ = now;
    }

    void reset()
    {
        intervalUs_ = 0;
        lastUs_ = 0;
    }

    uint32_t intervalUs() const { return intervalUs_; }
    uint32_t hz() const { return intervalUs_ ? 1000000u / intervalUs_ : 0; }

private:
    static constexpr uint8_t SHIFT = 3;
    uint32_t intervalUs_ = 0;
    uint32_t lastUs_ = 0;
};

