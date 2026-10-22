#pragma once

#include "rotation_sensor.hpp"

// 車輪速センサーの読み取り口。回転 (RotationSensor) に加えて、タイヤ直径で換算した車速を出す。
// 使う側は直径も歯数も知らない (換算はセンサーの側)。エンジン / クラッチ後は RotationSensor のまま。
class WheelSpeedSensor : public RotationSensor {
   public:
    virtual float mps() const = 0;  // 車速 [m/s]。直径が未設定 (0) なら 0
    float kmh() const { return mps() * 3.6f; }
};
