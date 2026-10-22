#include "clutch_motor.hpp"

namespace launch {

void ClutchMotor::initialize() {
    // TODO: configure PWM pins when hardware driver is decided
    on_ = false;
    lastCommand_ = 0.0f;
}

void ClutchMotor::on() {
    on_ = true;
}

void ClutchMotor::off() {
    on_ = false;
    lastCommand_ = 0.0f;
    // TODO: write(0) to motor driver
}

bool ClutchMotor::isOn() const {
    return on_;
}

void ClutchMotor::write(float positionCommand) {
    if (!on_)
        return;
    lastCommand_ = positionCommand;
    // TODO: inner PID — reads ClutchSensor, drives motor PWM
    // double output = innerPid_.compute(positionCommand, clutchSensor.convertedValue());
    // motor.write(output);
}

void ClutchMotor::setInnerPidGains(double kP, double kI, double kD) {
    innerPid_.setGains(kP, kI, kD);
}

}  // namespace launch
