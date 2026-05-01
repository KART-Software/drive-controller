#include "moving_average.hpp"

MovingAverage::MovingAverage(uint size) : size(size)
{
    values = new float[size];
    for (uint i = 0; i < size; i++)
    {
        values[i] = 0;
    }
}

void MovingAverage::add(float value)
{
    sum -= values[index];
    values[index] = value;
    sum += value;
    if (index >= size - 1)
    {
        index = 0;
    }
    else
    {
        index++;
    }
}

float MovingAverage::getAvg()
{
    return sum / size;
}