#pragma once

#include <Arduino.h>

class PulseCounter
{
public:
    PulseCounter(uint8_t pin);
    void begin();
    // tickISR から 1 ms ごとに呼ぶ。直近 PULSE_WINDOW_MS のパルス数 ÷ 経過時間で周波数を出す
    // (窓をずらしながら毎回更新。分解能は 1 パルス ÷ 窓、値は毎 ms 変わる)。nowUs は Clock の µs
    void update(uint32_t nowUs);
    float getFrequencyHz() const;
    float getRpm(uint8_t pulsesPerRev);
    // 最後の update() 時点のハードウェアカウンタ値 (16bit ラップあり)
    uint32_t count() const { return lastCount; }

    static constexpr uint32_t WINDOW_TICKS = 100;  // 窓 = 100 回の update (= 100 ms)

private:
    struct TimerInput
    {
        IMXRT_TMR_t *timer;
        uint8_t channel;
        uint8_t mux;
    };

    uint8_t pin;
    IMXRT_TMR_t *timer = nullptr;
    uint8_t channel = 0;
    uint32_t lastCount = 0;
    uint32_t total = 0;  // 16bit カウンタを差分で積み上げた累計 (一周しても差分は正しい)
    // 直近 WINDOW_TICKS 回分の (累計, 時刻)。head は次に書く位置
    uint32_t histCount[WINDOW_TICKS] = {};
    uint32_t histUs[WINDOW_TICKS] = {};
    uint32_t head = 0;
    uint32_t filled = 0;
    volatile float frequencyHz = 0;
    bool enabled = false;

    static bool getTimerInput(uint8_t pin, TimerInput &input);
    static void enableClock(IMXRT_TMR_t *timer);
    uint32_t readCounter() const;
};

