#pragma once

#include <Arduino.h>

#include "bite_point_estimator.hpp"
#include "bite_point_file.hpp"
#include "clutch_motor.hpp"
#include "proto/drive_controller.pb.h"
#include "interfaces/sensor/rotation_sensor.hpp"
#include "interfaces/sensor/wheel_speed_sensor.hpp"
#include "sensor/sensors.hpp"
#include "util/pid.hpp"

namespace launch {

// Launch control state machine with clutch engagement PID:
//
//   Idle → Ready → Approach → EngageControl → FullEngage → Idle
//
// engagement = wheel_rps_at_engine / engine_rps  (0=切断, 1=完全締結)
// PID drives clutch position to track a rising engagement target.
// Tire traction control is delegated entirely to the engine ECU.
class LaunchController {
   public:
    LaunchController(const RotationSensor& engine,
                     const WheelSpeedSensor& wheelRL,
                     const WheelSpeedSensor& wheelRR,
                     const ClutchSensor& clutch,
                     Flash& flash);

    // setConfig() / begin() は任意の順序で呼んで良い。begin() は保存値を
    // flash からロード、setConfig() は実行時パラメータを適用する。
    // どちらも複数回呼べる (例: SetConfigCmd で再適用)。
    void setConfig(const dc_LaunchConfig& cfg);  // 歯数は回転センサーが持つ (config RotationConfig)
    void begin();
    void update(bool launchRequested, uint32_t nowMs);  // nowMs は sysClock の ms

    enum class State { Idle, Ready, Approach, EngageControl, FullEngage };
    State state() const { return state_; }

   private:
    // -- sensors & actuator --
    const RotationSensor& engine_;
    const WheelSpeedSensor& wheelRL_;
    const WheelSpeedSensor& wheelRR_;
    const ClutchSensor& clutch_;
    ClutchMotor motor_;
    BitePointFile bitePointFile_;
    BitePointEstimator bitePoint_;

    // -- config --
    float launchRpmThreshold_ = 3000.0f;
    float clutchDepressThreshold_ = 20.0f;
    float approachSpeedPctPerS_ = 50.0f;
    float engagementRampRate_ = 0.3f;
    float bitePointMargin_ = 5.0f;
    float creepSpeedPctPerS_ = 10.0f;
    float gearRatio_ = 3.0f;
    float finalDriveRatio_ = 3.5f;

    // -- control --
    PID engagementPid_;
    State state_ = State::Idle;
    float approachPos_ = 0.0f;
    unsigned long lastUpdateMs_ = 0;
    float engageControlElapsed_ = 0.0f;
    // EngageControl 突入時の actual engagement を target ramp の起点に使う
    // (continuous start: target ジャンプを避ける)
    float engageControlStartEng_ = 0.0f;

    // -- helpers --
    float engineRps() const;
    float wheelRpsAtEngine() const;
    float clutchSlip() const;
    float engagement() const;  // wheel_rps_at_engine / engine_rps (0-1)
    float engineRpm() const;

    void transitionTo(State next);
    void handleIdle(bool launchRequested);
    void handleReady(bool launchRequested, uint32_t nowMs);
    void handleApproach(bool launchRequested, uint32_t nowMs);
    void handleEngageControl(bool launchRequested, uint32_t nowMs);
    void handleFullEngage(bool launchRequested);
};

}  // namespace launch
