#include "bite_point_estimator.hpp"

namespace launch {

void BitePointEstimator::begin() {
    bitePoint_ = file_.load();
    reset();
}

void BitePointEstimator::setParams(float slipThreshold, uint32_t debounceTicks, float emaAlpha) {
    slipThreshold_ = slipThreshold;
    debounceTicks_ = (debounceTicks > 0) ? debounceTicks : 1;
    emaAlpha_ = emaAlpha;
}

void BitePointEstimator::reset() {
    detected_ = false;
    consecutiveCount_ = 0;
    firstSlipPos_ = 0.0f;
}

bool BitePointEstimator::detect(float clutchPosition, float slip) {
    if (detected_)
        return false;

    if (slip > slipThreshold_) {
        if (consecutiveCount_ == 0) {
            firstSlipPos_ = clutchPosition;  // remember where slip started
        }
        consecutiveCount_++;

        if (consecutiveCount_ >= debounceTicks_) {
            // EMA: blend new observation with stored value
            bitePoint_ = emaAlpha_ * firstSlipPos_ + (1.0f - emaAlpha_) * bitePoint_;
            detected_ = true;
            file_.save(bitePoint_);
            return true;
        }
    } else {
        consecutiveCount_ = 0;
    }
    return false;
}

}  // namespace launch
