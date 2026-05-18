#pragma once

#include "util/pid.hpp"

namespace launch {

// Stub implementation — concrete motor driver to be added when hardware is decided.
// Mirrors the etc::DcMotor + etc::MotorController pattern:
//   inner PID drives position command (0-100%) → PWM output.
class ClutchMotor {
   public:
    void initialize();
    void on();
    void off();
    bool isOn() const;

    // positionCommand: 0.0 = fully disengaged, 100.0 = fully engaged
    void write(float positionCommand);

    void setInnerPidGains(double kP, double kI, double kD);

   private:
    PID innerPid_;
    bool on_ = false;
    float lastCommand_ = 0.0f;
};

}  // namespace launch
