#pragma once

#include <Arduino.h>

class PID {
   public:
    // direction: +1 でそのまま、-1 で出力符号反転。アクチュエータごとの方向は
    // 呼び出し側 (例: etc::MotorController) で明示的に指定する。
    PID(double kP = 0.0, double kI = 0.0, double kD = 0.0, int8_t direction = 1);
    double compute(double setPoint, double position);
    void setDirection(int8_t direction);
    void setGains(double kP, double kI, double kD);
    // errorSum / lastError / lastTime を初期化。状態遷移などで仕切り直す時に呼ぶ。
    void reset();
    // 積分項の出力寄与 |kI * errorSum| を limit 以下にクランプ (anti-windup)。
    // limit <= 0 で無効化 (デフォルト無効)。
    void setIntegralLimit(double limit) { integralLimit = limit; }

   private:
    double kP, kI, kD;
    double errorSum = 0.0;
    double lastError;
    unsigned long lastTime = 0;
    int8_t direction;
    double integralLimit = -1.0;  // <= 0 で無効
};
