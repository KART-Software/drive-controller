#include "pid.hpp"

PID::PID(double kP, double kI, double kD, int8_t direction) : kP(kP), kI(kI), kD(kD)
{
    setDirection(direction);
}

void PID::setDirection(int8_t direction)
{
    if (direction > 0)
    {
        this->direction = 1;
    }
    else
    {
        this->direction = -1;
    }
}

double PID::compute(double setPoint, double position, uint32_t nowUs)
{
    const double dtSec = (nowUs - lastUs) * 1e-6;
    lastUs = nowUs;
    double error = position - setPoint;
    double differential;

    if (first || dtSec <= 0.0)
    {
        differential = 0.0;
    }
    else
    {
        differential = (error - lastError) / dtSec;
        errorSum += error * dtSec;

        if (integralLimit > 0.0 && kI != 0.0) {
            double maxSum = integralLimit / fabs(kI);
            if (errorSum >  maxSum) errorSum =  maxSum;
            if (errorSum < -maxSum) errorSum = -maxSum;
        }
    }
    first = false;
    lastError = error;

    return -direction * (kP * error + kI * errorSum + kD * differential);
}

void PID::setGains(double kP, double kI, double kD)
{
    this->kP = kP;
    this->kI = kI;
    this->kD = kD;
}

void PID::reset()
{
    errorSum = 0.0;
    lastError = 0.0;
    first = true;
}