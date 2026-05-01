#ifndef _IMU_H_
#define _IMU_H_

#include <Arduino.h>
#include <SPI.h>

class Imu
{
public:
    virtual ~Imu() = default;
    virtual void begin() = 0;
    virtual void read() = 0;

    // Accelerometer data in mg (milli-g)
    float accel[3] = {0, 0, 0}; // X, Y, Z

    // Gyroscope data in dps (degrees per second)
    float gyro[3] = {0, 0, 0}; // X, Y, Z
};

#endif // _IMU_H_
