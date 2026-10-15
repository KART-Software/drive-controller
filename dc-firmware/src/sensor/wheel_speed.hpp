#pragma once

#include <Arduino.h>
#include <FreqMeasureMulti.h>

// FlexPWM 入力キャプチャ (FreqMeasureMulti) による周波数センサー。
// QuadTimer の取れない FlexPWM ピン (車輪速ピン 24/25/28/36) 用。
// 公開インターフェースは PulseCounter と同じ (getFrequencyHz / getRpm / count)
// なので auto_shifter / launch_controller から同様に const 参照で受け取れる。
//
// 各インスタンスは別々の FlexPWM サブモジュールのピンに割り当てること
// (同一サブモジュールの 2 ピンは同時計測できない)。FreqMeasureMulti は
// 内部割り込みでエッジ周期を計測する (最大 8 ch)。
class WheelSpeedSensor {
   public:
    explicit WheelSpeedSensor(uint8_t pin) : pin_(pin) {}

    void begin();
    void update(uint32_t nowMs);  // tickISR から 1 ms ごと (nowMs は sysClock の ms): この間に来た周期の平均→Hz (来なければ前の値)、無入力が続けば 0 に減衰

    float getFrequencyHz() const { return frequencyHz_; }
    float getRpm(uint8_t pulsesPerRev) const {
        return pulsesPerRev == 0 ? 0.0f : frequencyHz_ * 60.0f / pulsesPerRev;
    }
    uint32_t count() const { return pulseCount_; }

   private:
    uint8_t pin_;
    FreqMeasureMulti fm_;
    bool enabled_ = false;
    volatile float frequencyHz_ = 0.0f;
    uint32_t pulseCount_ = 0;
    uint32_t lastEdgeMs_ = 0;
    // 一定時間エッジが来なければ停止とみなし 0Hz にする (停車判定用)。
    static constexpr uint32_t STALE_MS = 300;
};
