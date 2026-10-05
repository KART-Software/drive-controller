#pragma once

#include "dcmotor.hpp"
#include "sensor/sensors.hpp"
#include "util/pid.hpp"

namespace etc {

class MotorController {
   public:
    MotorController(const EtcTarget& target, const Tps& tps);
    void initialize();
    void cycle();
    void setMotorOn();
    void setMotorOff();
    bool isOn();
    void setPidGains(double kP, double kI, double kD);
    // 直近の実印加 duty (%)。ISR が書き loop が読む: float の 32bit ストアは
    // Cortex-M7 でアトミックなので保護不要。モーター停止中は 0 (setMotorOff で
    // クリア = 「常に実印加値」の不変条件。SD ログ・実験ガードが参照する)。
    float lastOutput() const { return lastOutput_; }

   private:
    DcMotor dcMotor = DcMotor();
    PID pid{0.0, 0.0, 0.0, MOTOR_DIRECTION};
    const EtcTarget& target;
    const Tps& tps;
    const unsigned long cycleTime = MOTOR_CONTROLL_CYCLE_TIME;
    double output;
    volatile float lastOutput_ = 0.0f;
};

}  // namespace etc

