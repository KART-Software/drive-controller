#include "launch_controller.hpp"

#include <Arduino.h>

namespace launch {

LaunchController::LaunchController(const SensorHub& sensorHub) : hub_(sensorHub) {}

void LaunchController::setConfig(const dc_LaunchConfig& cfg,
                                 uint32_t engineTeeth,
                                 uint32_t wheelTeethFront,
                                 uint32_t wheelTeethRear) {
    launchRpmThreshold_ = cfg.launch_rpm_threshold;
    clutchDepressThreshold_ = cfg.clutch_depress_threshold;
    approachSpeedPctPerS_ = cfg.approach_speed_pct_per_s;
    initialEngagement_ = cfg.initial_engagement;
    engagementRampRate_ = cfg.engagement_ramp_rate;
    bitePointMargin_ = cfg.bite_point_margin;
    creepSpeedPctPerS_ = cfg.creep_speed_pct_per_s;
    gearRatio_ = (cfg.gear_ratio > 0.0f) ? cfg.gear_ratio : 1.0f;
    finalDriveRatio_ = (cfg.final_drive_ratio > 0.0f) ? cfg.final_drive_ratio : 1.0f;
    engineTeeth_ = (engineTeeth > 0) ? engineTeeth : 1;
    wheelTeethFront_ = (wheelTeethFront > 0) ? wheelTeethFront : 1;
    wheelTeethRear_ = (wheelTeethRear > 0) ? wheelTeethRear : 1;

    bitePoint_.setParams(cfg.slip_detect_threshold, cfg.slip_detect_debounce, cfg.bite_point_ema_alpha);

    if (cfg.has_engagement_pid) {
        engagementPid_.setGains(cfg.engagement_pid.k_p, cfg.engagement_pid.k_i, cfg.engagement_pid.k_d);
    }
    if (cfg.has_inner_pid) {
        motor_.setInnerPidGains(cfg.inner_pid.k_p, cfg.inner_pid.k_i, cfg.inner_pid.k_d);
    }
}

void LaunchController::begin(Flash& flash) {
    static BitePointFile file(flash);
    bitePointFile_ = &file;
    bitePoint_.begin(file);
}

void LaunchController::update(bool launchRequested) {
    switch (state_) {
        case State::Idle:
            handleIdle(launchRequested);
            break;
        case State::Ready:
            handleReady(launchRequested);
            break;
        case State::Approach:
            handleApproach(launchRequested);
            break;
        case State::EngageControl:
            handleEngageControl(launchRequested);
            break;
        case State::FullEngage:
            handleFullEngage(launchRequested);
            break;
    }
}

// ── helpers ──────────────────────────────────────────────────────────

float LaunchController::engineRps() const {
    return hub_.engineRpm() / static_cast<float>(engineTeeth_);
}

float LaunchController::engineRpm() const {
    return engineRps() * 60.0f;
}

float LaunchController::wheelRpsAtEngine() const {
    // Average rear wheel Hz → RPS → multiply by total drive ratio
    float hz = (hub_.wheelSpeedRL() + hub_.wheelSpeedRR()) * 0.5f;
    float wheelRps = hz / static_cast<float>(wheelTeethRear_);
    return wheelRps * gearRatio_ * finalDriveRatio_;
}

float LaunchController::clutchSlip() const {
    float eRps = engineRps();
    if (eRps < 0.1f)
        return 0.0f;
    float slip = (eRps - wheelRpsAtEngine()) / eRps;
    if (slip < 0.0f)
        return 0.0f;
    if (slip > 1.0f)
        return 1.0f;
    return slip;
}

float LaunchController::engagement() const {
    return 1.0f - clutchSlip();
}

void LaunchController::transitionTo(State next) {
    state_ = next;
}

// ── state handlers ────────────────────────────────────────────────────

void LaunchController::handleIdle(bool launchRequested) {
    if (launchRequested) {
        motor_.initialize();
        motor_.on();
        transitionTo(State::Ready);
    }
}

void LaunchController::handleReady(bool launchRequested) {
    if (!launchRequested) {
        motor_.off();
        transitionTo(State::Idle);
        return;
    }

    bool engineHighEnough = engineRpm() >= launchRpmThreshold_;
    bool clutchDepressed = hub_.clutch().convertedValue() < clutchDepressThreshold_;
    if (engineHighEnough && clutchDepressed) {
        // Start approach from current clutch sensor position
        approachPos_ = static_cast<float>(hub_.clutch().convertedValue());
        lastUpdateMs_ = millis();
        bitePoint_.reset();
        transitionTo(State::Approach);
    }
}

void LaunchController::handleApproach(bool launchRequested) {
    if (!launchRequested) {
        motor_.off();
        transitionTo(State::Idle);
        return;
    }

    unsigned long now = millis();
    float dtS = static_cast<float>(now - lastUpdateMs_) * 0.001f;
    lastUpdateMs_ = now;

    // Phase 1: fast ramp toward (bitePoint - margin)
    // Phase 2: creep forward once margin target reached
    float marginTarget = bitePoint_.bitePoint() - bitePointMargin_;
    if (marginTarget < 0.0f)
        marginTarget = 0.0f;

    float speed = (approachPos_ < marginTarget) ? approachSpeedPctPerS_ : creepSpeedPctPerS_;
    float delta = speed * dtS;
    approachPos_ += delta;
    if (approachPos_ > 100.0f)
        approachPos_ = 100.0f;
    motor_.write(approachPos_);

    // Detect clutch engagement onset — update bite point via EMA and transition
    float eng = engagement();
    // BitePointEstimator detects slip (1-engagement), so pass slip value
    if (bitePoint_.detect(approachPos_, 1.0f - eng)) {
        engageControlElapsed_ = 0.0f;
        lastUpdateMs_ = millis();
        transitionTo(State::EngageControl);
    }
}

void LaunchController::handleEngageControl(bool launchRequested) {
    if (!launchRequested) {
        motor_.off();
        transitionTo(State::Idle);
        return;
    }

    unsigned long now = millis();
    float dtS = static_cast<float>(now - lastUpdateMs_) * 0.001f;
    lastUpdateMs_ = now;
    engageControlElapsed_ += dtS;

    // Target engagement ramps from initialEngagement toward 1.0
    float targetEng = initialEngagement_ + engagementRampRate_ * engageControlElapsed_;
    if (targetEng > 1.0f)
        targetEng = 1.0f;

    float actual = engagement();

    // PID 規約: error = actual - target (現在値 - 目標値)
    // actual < target → error 負 → output 正 → クラッチ位置を上げ engagement を増やす
    double output = engagementPid_.compute(targetEng, actual);

    float posCmd = bitePoint_.bitePoint() + static_cast<float>(output);
    if (posCmd < 0.0f)
        posCmd = 0.0f;
    if (posCmd > 100.0f)
        posCmd = 100.0f;
    motor_.write(posCmd);

    // Complete when fully engaged
    if (targetEng >= 1.0f && actual > 0.98f) {
        transitionTo(State::FullEngage);
    }
}

void LaunchController::handleFullEngage(bool launchRequested) {
    if (!launchRequested) {
        motor_.off();
        transitionTo(State::Idle);
        return;
    }

    // Drive clutch fully engaged
    motor_.write(100.0f);

    // Complete when clutch sensor reports near-full engagement
    if (hub_.clutch().convertedValue() >= 99.0) {
        motor_.off();
        transitionTo(State::Idle);
    }
}

}  // namespace launch
