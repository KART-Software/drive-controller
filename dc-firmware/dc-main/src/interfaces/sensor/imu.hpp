#pragma once

#include <array>

// 6 軸 IMU の読み取り口。値は移動平均 (ノイズ低減後)。どの文脈から読んでもよい。
class Imu {
   public:
    using Vec3 = std::array<float, 3>;  // X, Y, Z

    virtual ~Imu() = default;
    virtual void begin() = 0;

    virtual Vec3 accel() const = 0;  // 加速度 [mg]
    virtual Vec3 gyro() const = 0;   // 角速度 [dps]
};
