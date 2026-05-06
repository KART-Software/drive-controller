#pragma once

#include <Arduino.h>
#include "constants.hpp"

class PID {
   public:
    PID(double kP = 0.0, double kI = 0.0, double kD = 0.0, int8_t direction = MOTOR_DIRECTION);
    double compute(double setPoint, double position);
    void setDirection(int8_t direction);
    void setGains(double kP, double kI, double kD);

   private:
    double kP, kI, kD;
    double errorSum = 0.0;
    double lastError;
    unsigned long lastTime = 0;
    int8_t direction;
};
