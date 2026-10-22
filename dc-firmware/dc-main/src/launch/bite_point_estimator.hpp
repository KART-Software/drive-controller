#pragma once

#include "bite_point_file.hpp"

namespace launch {

// Learns the clutch bite point (meet point) during launch Approach phase.
// Uses debounce (consecutive N ticks above threshold) to filter noise,
// and EMA to smooth the learned value across launches.
class BitePointEstimator {
   public:
    explicit BitePointEstimator(BitePointFile& file) : file_(file) {}

    void begin();

    void setParams(float slipThreshold, uint32_t debounceTicks, float emaAlpha);

    float bitePoint() const { return bitePoint_; }

    // Call on each Approach cycle tick.
    // Returns true when slip is confirmed (debounce passed), at which point
    // bitePoint() is updated via EMA and auto-saved.
    bool detect(float clutchPosition, float slip);

    void reset();

   private:
    BitePointFile& file_;
    float bitePoint_ = 50.0f;
    bool detected_ = false;

    // params
    float slipThreshold_ = 0.01f;
    uint32_t debounceTicks_ = 3;
    float emaAlpha_ = 0.3f;

    // debounce state
    uint32_t consecutiveCount_ = 0;
    float firstSlipPos_ = 0.0f;  // position when slip first started
};

}  // namespace launch
