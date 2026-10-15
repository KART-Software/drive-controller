#pragma once

#include <Arduino.h>

#include "util/stop_watch.hpp"

/**
 * Measures the call frequency by timing the interval between `tick()` calls (StopWatch).
 * The interval is smoothed with an exponential moving average (EMA):
 *   interval = (interval * (2^SHIFT - 1) + delta) >> SHIFT
 * SHIFT = 3 -> time constant ~8 samples.
 */
class FrequencyMeter
{
public:
    explicit FrequencyMeter(const Clock& clock) : watch_(clock) {}

    void tick()
    {
        const uint32_t delta = watch_.lapUs();  // µs
        if (started_)
        {
            constexpr uint32_t mask = (1u << SHIFT) - 1u;
            intervalUs_ = intervalUs_ ? (intervalUs_ * mask + delta) >> SHIFT : delta;
        }
        started_ = true;
    }

    void reset()
    {
        intervalUs_ = 0;
        started_ = false;
    }

    uint32_t intervalUs() const { return intervalUs_; }
    uint32_t hz() const { return intervalUs_ ? 1000000u / intervalUs_ : 0; }

private:
    static constexpr uint8_t SHIFT = 3;
    uint32_t intervalUs_ = 0;
    StopWatch watch_;
    bool started_ = false;
};

