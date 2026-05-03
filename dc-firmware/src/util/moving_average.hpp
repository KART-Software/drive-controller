#ifndef _MOVING_AVERAGE_H_
#define _MOVING_AVERAGE_H_

#include <Arduino.h>

class MovingAverage
{
public:
    MovingAverage(uint size = 100);
    void add(float value);
    float getAvg() const;

private:
    const uint size;
    float *values;
    float sum = 0;
    uint index = 0;
};

#endif