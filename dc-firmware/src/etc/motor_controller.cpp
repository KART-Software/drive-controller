#include "motor_controller.hpp"

namespace etc {

MotorController::MotorController(const EtcTarget& target, const Tps& tps) : target(target), tps(tps) {}

void MotorController::initialize() {
    dcMotor.initialize();
}

void MotorController::cycle() {
    double target_ = target.getTarget();
    double tp = tps.convertedValue();
    output = pid.compute(target_, tp);
    dcMotor.write(output);
}

void MotorController::setMotorOn() {
    pid.reset();
    dcMotor.on();
}

void MotorController::setMotorOff() {
    dcMotor.off();
}

bool MotorController::isOn() {
    return dcMotor.isOn();
}

void MotorController::setPidGains(double kP, double kI, double kD) {
    pid.setGains(kP, kI, kD);
}

}  // namespace etc