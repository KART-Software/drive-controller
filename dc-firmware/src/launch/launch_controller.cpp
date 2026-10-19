#include "launch_controller.hpp"

#include <Arduino.h>

namespace launch {

LaunchController::LaunchController(const RotationSensor& engine,
                                   const RotationSensor& wheelRL,
                                   const RotationSensor& wheelRR,
                                   const ClutchSensor& clutch,
                                   Flash& flash)
    : engine_(engine),
      wheelRL_(wheelRL),
      wheelRR_(wheelRR),
      clutch_(clutch),
      bitePointFile_(flash),
      bitePoint_(bitePointFile_) {}

void LaunchController::setConfig(const dc_LaunchConfig& cfg) {
    launchRpmThreshold_ = cfg.launch_rpm_threshold;
    clutchDepressThreshold_ = cfg.clutch_depress_threshold;
    approachSpeedPctPerS_ = cfg.approach_speed_pct_per_s;
    engagementRampRate_ = cfg.engagement_ramp_rate;
    bitePointMargin_ = cfg.bite_point_margin;
    creepSpeedPctPerS_ = cfg.creep_speed_pct_per_s;
    gearRatio_ = (cfg.gear_ratio > 0.0f) ? cfg.gear_ratio : 1.0f;
    finalDriveRatio_ = (cfg.final_drive_ratio > 0.0f) ? cfg.final_drive_ratio : 1.0f;

    bitePoint_.setParams(cfg.slip_detect_threshold, cfg.slip_detect_debounce, cfg.bite_point_ema_alpha);

    if (cfg.has_engagement_pid) {
        engagementPid_.setGains(cfg.engagement_pid.k_p, cfg.engagement_pid.k_i, cfg.engagement_pid.k_d);
    }
    if (cfg.has_inner_pid) {
        motor_.setInnerPidGains(cfg.inner_pid.k_p, cfg.inner_pid.k_i, cfg.inner_pid.k_d);
    }
}

void LaunchController::begin() {
    bitePoint_.begin();
}

void LaunchController::update(bool launchRequested, uint32_t nowMs) {
    switch (state_) {
        case State::Idle:
            handleIdle(launchRequested);
            break;
        case State::Ready:
            handleReady(launchRequested, nowMs);
            break;
        case State::Approach:
            handleApproach(launchRequested, nowMs);
            break;
        case State::EngageControl:
            handleEngageControl(launchRequested, nowMs);
            break;
        case State::FullEngage:
            handleFullEngage(launchRequested);
            break;
    }
}

// ── helpers ──────────────────────────────────────────────────────────

float LaunchController::engineRps() const {
    return engine_.rpm() / 60.0f;
}

float LaunchController::engineRpm() const {
    return engineRps() * 60.0f;
}

float LaunchController::wheelRpsAtEngine() const {
    // 後輪の回転 (左右平均) → エンジン換算 (ギア比 × 最終減速比)
    float wheelRps = (wheelRL_.rpm() + wheelRR_.rpm()) * 0.5f / 60.0f;
    return wheelRps * gearRatio_ * finalDriveRatio_;
}

float LaunchController::clutchSlip() const {
    float eRps = engineRps();
    // エンジン停止 / 極低回転時は計測無効。「完全に滑っている」扱い (1.0) を返し、
    // engagement() を 0 にする。0 を返すと engagement=1 になり EngageControl 中に
    // 誤って FullEngage 遷移してしまうため避ける。
    if (eRps < 0.1f)
        return 1.0f;
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
    if (next == State::Idle) {
        motor_.off();
    }
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

void LaunchController::handleReady(bool launchRequested, uint32_t nowMs) {
    if (!launchRequested) {
        transitionTo(State::Idle);
        return;
    }

    bool engineHighEnough = engineRpm() >= launchRpmThreshold_;
    bool clutchDepressed = clutch_.convertedValue() < clutchDepressThreshold_;
    if (engineHighEnough && clutchDepressed) {
        approachPos_ = static_cast<float>(clutch_.convertedValue());
        lastUpdateMs_ = nowMs;
        bitePoint_.reset();
        transitionTo(State::Approach);
    }
}

void LaunchController::handleApproach(bool launchRequested, uint32_t nowMs) {
    if (!launchRequested) {
        transitionTo(State::Idle);
        return;
    }

    float dtS = static_cast<float>(nowMs - lastUpdateMs_) * 0.001f;
    lastUpdateMs_ = nowMs;

    float marginTarget = bitePoint_.bitePoint() - bitePointMargin_;
    if (marginTarget < 0.0f)
        marginTarget = 0.0f;

    float speed = (approachPos_ < marginTarget) ? approachSpeedPctPerS_ : creepSpeedPctPerS_;
    float delta = speed * dtS;
    approachPos_ += delta;
    // bitePoint + margin を超えて creep しない。bitePoint 推定が低めに外れた
    // 場合の保険であって、ここでハングしたら A-1 のタイムアウト側で抜ける。
    float maxApproachPos = bitePoint_.bitePoint() + bitePointMargin_;
    if (maxApproachPos > 100.0f)
        maxApproachPos = 100.0f;
    if (approachPos_ > maxApproachPos)
        approachPos_ = maxApproachPos;
    motor_.write(approachPos_);

    if (bitePoint_.detect(approachPos_, clutchSlip())) {
        engageControlElapsed_ = 0.0f;
        engageControlStartEng_ = engagement();
        engagementPid_.reset();
        lastUpdateMs_ = nowMs;
        transitionTo(State::EngageControl);
    }
}

void LaunchController::handleEngageControl(bool launchRequested, uint32_t nowMs) {
    if (!launchRequested) {
        transitionTo(State::Idle);
        return;
    }

    float dtS = static_cast<float>(nowMs - lastUpdateMs_) * 0.001f;
    lastUpdateMs_ = nowMs;
    engageControlElapsed_ += dtS;

    float targetEng = engageControlStartEng_ + engagementRampRate_ * engageControlElapsed_;
    if (targetEng > 1.0f)
        targetEng = 1.0f;

    float actual = engagement();

    // PID 規約: error = actual - target (現在値 - 目標値)
    // actual < target → error 負 → output 正 → クラッチ位置を上げ engagement を増やす
    double output = engagementPid_.compute(targetEng, actual, nowMs * 1000u);

    float posCmd = bitePoint_.bitePoint() + static_cast<float>(output);
    if (posCmd < 0.0f)
        posCmd = 0.0f;
    if (posCmd > 100.0f)
        posCmd = 100.0f;
    motor_.write(posCmd);

    // FullEngage 遷移条件: actual が target を追いつくか、ハングプリベントの
    // タイムアウト (target=1 到達後一定時間) を超えた場合。
    const float kEngageMaxDurationS = 3.0f;
    bool reached = targetEng >= 1.0f && actual > 0.98f;
    bool timedOut = engageControlElapsed_ > kEngageMaxDurationS;
    if (reached || timedOut) {
        transitionTo(State::FullEngage);
    }
}

void LaunchController::handleFullEngage(bool launchRequested) {
    if (!launchRequested) {
        transitionTo(State::Idle);
        return;
    }

    motor_.write(100.0f);

    if (clutch_.convertedValue() >= 99.0) {
        transitionTo(State::Idle);
    }
}

}  // namespace launch
