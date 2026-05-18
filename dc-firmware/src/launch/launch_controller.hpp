#pragma once

#include <Arduino.h>

#include "bite_point_estimator.hpp"
#include "bite_point_file.hpp"
#include "clutch_motor.hpp"
#include "proto/drive_controller.pb.h"
#include "sensor/sensor_hub.hpp"
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
    explicit LaunchController(const SensorHub& sensorHub);

    void setConfig(const dc_LaunchConfig& cfg, uint32_t engineTeeth, uint32_t wheelTeethFront, uint32_t wheelTeethRear);
    void begin(Flash& flash);
    void update(bool launchRequested);

    enum class State { Idle, Ready, Approach, EngageControl, FullEngage };
    State state() const { return state_; }

   private:
    // -- sensors & actuator --
    const SensorHub& hub_;
    ClutchMotor motor_;
    BitePointEstimator bitePoint_;
    BitePointFile* bitePointFile_ = nullptr;

    // -- config --
    float launchRpmThreshold_ = 3000.0f;
    float clutchDepressThreshold_ = 20.0f;
    float approachSpeedPctPerS_ = 50.0f;
    float initialEngagement_ = 0.5f;
    float engagementRampRate_ = 0.3f;
    float bitePointMargin_ = 5.0f;
    float creepSpeedPctPerS_ = 10.0f;
    float gearRatio_ = 3.0f;
    float finalDriveRatio_ = 3.5f;
    uint32_t engineTeeth_ = 1;
    uint32_t wheelTeethFront_ = 1;
    uint32_t wheelTeethRear_ = 1;

    // -- control --
    PID engagementPid_;
    State state_ = State::Idle;
    float approachPos_ = 0.0f;
    unsigned long lastUpdateMs_ = 0;
    float engageControlElapsed_ = 0.0f;

    // -- helpers --
    float engineRps() const;
    float wheelRpsAtEngine() const;
    float clutchSlip() const;
    float engagement() const;  // wheel_rps_at_engine / engine_rps (0-1)
    float engineRpm() const;

    void transitionTo(State next);
    void handleIdle(bool launchRequested);
    void handleReady(bool launchRequested);
    void handleApproach(bool launchRequested);
    void handleEngageControl(bool launchRequested);
    void handleFullEngage(bool launchRequested);
};

}  // namespace launch
