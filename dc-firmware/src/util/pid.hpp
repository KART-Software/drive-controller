#pragma once

#include <Arduino.h>

class PID {
   public:
    // direction: +1 でそのまま、-1 で出力符号反転。アクチュエータごとの方向は
    // 呼び出し側 (例: etc::MotorController) で明示的に指定する。
    PID(double kP = 0.0, double kI = 0.0, double kD = 0.0, int8_t direction = 1);
    // nowUs = 呼び出し時の時刻 [µs] (sysClock)。dt は前回の compute との差を中で取る (uint32 の wrap は差で吸収)。
    // reset() 直後の 1 回は微分も積分もしない
    double compute(double setPoint, double position, uint32_t nowUs);
    void setDirection(int8_t direction);
    void setGains(double kP, double kI, double kD);
    // errorSum / lastError を初期化。状態遷移などで仕切り直す時に呼ぶ。
    void reset();
    // 積分項の出力寄与 |kI * errorSum| を limit 以下にクランプ (anti-windup)。
    // limit <= 0 で無効化 (デフォルト無効)。
    void setIntegralLimit(double limit) { integralLimit = limit; }

   private:
    double kP, kI, kD;
    double errorSum = 0.0;
    double lastError;
    uint32_t lastUs = 0;
    bool first = true;  // reset() 直後 (前回の誤差と時刻がない)
    int8_t direction;
    double integralLimit = -1.0;  // <= 0 で無効
};
