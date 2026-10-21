#pragma once

#include "adc.hpp"
#include "interfaces/sensor/imu.hpp"
#include "interfaces/sensor/rotation_sensor.hpp"
#include "sensors.hpp"
#include "util/toggle_switch.hpp"

// SensorHub の「見るだけ」の口。各センサーへの const 参照の束で、書き換えるメソッドを持たない (型で保証する。
// const の付け忘れでは抜けられない)。見るだけの部品 (CanController、ExperimentRunner、telemetry、ログ) はこれを受け取り、
// SensorHub そのもの (書き換え可) を持つのは main.cpp と Configurator だけ。SensorHub::view() で得る。
class SensorView {
   public:
    struct Refs {
        const Apps& apps1;
        const Apps& apps2;
        const Tps& tps1;
        const Tps& tps2;
        const Ittr& ittr;
        const Bps& bps;
        const EtcTarget& target;
        const GearPositionSensor& gps;
        const ClutchSensor& clutch;
        const Imu& imu;
        const Adc& adc;
        const RotationSensor& pulseEngine;
        const RotationSensor& pulseClutchRpm;
        const RotationSensor& pulseWheelFL;
        const RotationSensor& pulseWheelFR;
        const RotationSensor& pulseWheelRL;
        const RotationSensor& pulseWheelRR;
        const ToggleSwitch& shutdownSig;
    };
    explicit SensorView(const Refs& refs) : r_(refs) {}

    const Apps& apps1() const { return r_.apps1; }
    const Apps& apps2() const { return r_.apps2; }
    const Tps& tps1() const { return r_.tps1; }
    const Tps& tps2() const { return r_.tps2; }
    const Ittr& ittr() const { return r_.ittr; }
    const Bps& bps() const { return r_.bps; }
    const EtcTarget& target() const { return r_.target; }
    const GearPositionSensor& gps() const { return r_.gps; }
    const ClutchSensor& clutch() const { return r_.clutch; }
    const Imu& imu() const { return r_.imu; }
    const Adc& adc() const { return r_.adc; }
    const RotationSensor& pulseEngine() const { return r_.pulseEngine; }
    const RotationSensor& pulseClutchRpm() const { return r_.pulseClutchRpm; }
    const RotationSensor& pulseWheelFL() const { return r_.pulseWheelFL; }
    const RotationSensor& pulseWheelFR() const { return r_.pulseWheelFR; }
    const RotationSensor& pulseWheelRL() const { return r_.pulseWheelRL; }
    const RotationSensor& pulseWheelRR() const { return r_.pulseWheelRR; }
    // SHUTDOWN 回路出力 (点火系 AND の結果)。isOn()==true で ETC 許可
    const ToggleSwitch& shutdownSig() const { return r_.shutdownSig; }

   private:
    Refs r_;
};
