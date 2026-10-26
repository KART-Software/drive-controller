#pragma once

#include "edge_rotation_sensor.hpp"
#include "interfaces/sensor/wheel_speed_sensor.hpp"

// 車輪速センサー = EdgeRotationSensor (回転) + タイヤ直径。回転の計測は中の EdgeRotationSensor にそのまま任せ、
// 車速はそこから読んだ rpm に直径を掛けて出す。継承でなく合成 (RotationSensor のダイヤモンドを避ける)。
// 呼ぶ文脈は EdgeRotationSensor と同じ (begin: setup、setParams / setDiameterMm: loop、update: tick、読みはどこからでも)。
class EdgeWheelSpeedSensor final : public WheelSpeedSensor {
   public:
    explicit EdgeWheelSpeedSensor(EdgeSource& source) : core_(source) {}

    bool begin() { return core_.begin(); }
    void setParams(const RotationParams& p) { core_.setParams(p); }
    // タイヤ直径 [mm] (config RotationConfig の wheel_front / wheel_rear)。loop から。float 1 語の書き込みなので割り込みは止めない
    void setDiameterMm(float mm) { diameterMm_ = mm; }
    void update(uint64_t nowUs) { core_.update(nowUs); }

    RotationSample sample() const override { return core_.sample(); }
    uint32_t count() const override { return core_.count(); }
    uint32_t pulses() const override { return core_.pulses(); }
    float mps() const override { return core_.rpm() * 3.14159265f * diameterMm_ / 1000.0f / 60.0f; }

   private:
    EdgeRotationSensor core_;
    volatile float diameterMm_ = 0.0f;
};
