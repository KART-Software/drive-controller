#include "starter_controller.hpp"

#include "constants.hpp"
#include "util/log/debug_logger.hpp"

namespace starter {

void StarterController::begin() {
    pinMode(STARTER_RELAY_PIN, OUTPUT);
    setRelay(false);
}

void StarterController::setConfig(const dc_StarterConfig& cfg) {
    runningRpm_ = cfg.engine_running_rpm;
    runningConfirmMs_ = cfg.running_confirm_ms;
    minHoldMs_ = cfg.min_hold_ms;
}

float StarterController::engineRpm() const {
    return engine_.rpm();
}

void StarterController::setRelay(bool on) {
    digitalWrite(STARTER_RELAY_PIN, on ? HIGH : LOW);
}

void StarterController::tick(bool switchPressed, uint32_t nowMs) {
    const bool pressedEdge = switchPressed && !prevSwitch_;
    prevSwitch_ = switchPressed;
    const float rpm = engineRpm();
    const bool above = rpm > runningRpm_;

    switch (state_) {
        case State::Off:
            if (!pressedEdge)
                break;
            if (above) {
                DebugLogger::log("starter: not started (engine already running, %d rpm)", (int)rpm);
                break;
            }
            state_ = State::Cranking;
            crankStartMs_ = nowMs;
            aboveRpm_ = false;
            setRelay(true);
            DebugLogger::log("starter: on");
            break;

        case State::Cranking: {
            // エンジンがかかった: しきい値を超えた状態が続いた時間で見る (途中で落ちたら数え直す)
            if (above && !aboveRpm_)
                aboveSinceMs_ = nowMs;
            aboveRpm_ = above;
            const bool engineStarted = aboveRpm_ && nowMs - aboveSinceMs_ >= runningConfirmMs_;
            const bool holdOver = nowMs - crankStartMs_ >= minHoldMs_;
            if (engineStarted || (holdOver && !switchPressed)) {
                state_ = State::Off;
                setRelay(false);
                DebugLogger::log("starter: off (%s, %lu ms)", engineStarted ? "engine running" : "switch released",
                                 (unsigned long)(nowMs - crankStartMs_));
            }
            break;
        }
    }
}

}  // namespace starter
