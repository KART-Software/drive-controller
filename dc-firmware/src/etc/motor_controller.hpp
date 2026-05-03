#ifndef _MOTOR_CONTROLLER_H_
#define _MOTOR_CONTROLLER_H_

#include "dcmotor.hpp"
#include "pid.hpp"
#include "sensor/sensors.hpp"

class MotorController {
   public:
    MotorController(const Target& target, const Tps& tps);
    void initialize();
    void cycle();
    void setMotorOn();
    void setMotorOff();
    bool isOn();
    void setPidGains(double kP, double kI, double kD);

   private:
    DcMotor dcMotor = DcMotor();
    PID pid = PID();
    const Target& target;
    const Tps& tps;
    const unsigned long cycleTime = MOTOR_CONTROLL_CYCLE_TIME;
    double output;
};

#endif