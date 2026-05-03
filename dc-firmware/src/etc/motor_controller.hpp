#ifndef _MOTOR_CONTROLLER_H_
#define _MOTOR_CONTROLLER_H_

#include "dcmotor.hpp"
#include "util/pid.hpp"
#include "sensor/sensors.hpp"

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

   private:
    DcMotor dcMotor = DcMotor();
    PID pid = PID();
    const EtcTarget& target;
    const Tps& tps;
    const unsigned long cycleTime = MOTOR_CONTROLL_CYCLE_TIME;
    double output;
};

}  // namespace etc

#endif