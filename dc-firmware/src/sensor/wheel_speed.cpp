#include "wheel_speed.hpp"

void WheelSpeedSensor::begin() {
    enabled_ = fm_.begin(pin_);  // FlexPWM 入力キャプチャを設定 (立ち上がり周期)
    frequencyHz_ = 0.0f;
    lastEdgeMs_ = millis();
}

void WheelSpeedSensor::update() {
    if (!enabled_) {
        frequencyHz_ = 0.0f;
        return;
    }
    uint32_t sum = 0;
    uint32_t n = 0;
    while (fm_.available()) {
        sum += fm_.read();  // 1 周期あたりのタイマカウント
        n++;
    }
    uint32_t now = millis();
    if (n > 0) {
        // 周期カウントを平均してから周波数へ変換 (平滑化)
        frequencyHz_ = FreqMeasureMulti::countToFrequency(sum / n);
        pulseCount_ += n;
        lastEdgeMs_ = now;
    } else if ((uint32_t)(now - lastEdgeMs_) > STALE_MS) {
        frequencyHz_ = 0.0f;  // 一定時間エッジ無し → 停止
    }
}
