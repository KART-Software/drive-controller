#pragma once

#include <Arduino.h>

// 固定サイズ N の移動平均。add() を ISR から、getAvg() をメインループから
// 呼ぶ前提で、getAvg() は noInterrupts() で sum の読み取りを守る。
template <uint N>
class MovingAverage {
   public:
    void add(float value) {
        sum -= values[index];
        values[index] = value;
        sum += value;
        if (index >= N - 1) {
            index = 0;
        } else {
            index++;
        }
    }

    float getAvg() const {
        // ISR からの add() による sum 部分更新中の読み取りを避ける
        noInterrupts();
        float s = sum;
        interrupts();
        return s / static_cast<float>(N);
    }

   private:
    float values[N] = {};
    float sum = 0.0f;
    uint index = 0;
};
