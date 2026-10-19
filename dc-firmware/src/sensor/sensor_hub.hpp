#pragma once

#include "adc.hpp"
#include "constants.hpp"
#include "iam20680hp.hpp"
#include "edge_rotation_sensor.hpp"
#include "freqmeasure_edge_source.hpp"
#include "interfaces/sensor/rotation_sensor.hpp"
#include "quadtimer_edge_source.hpp"
#include "sensors.hpp"
#include "util/toggle_switch.hpp"

class SensorHub {
   public:
    // clock: ADC の SPI 待ちとサンプリング頻度の計測に渡す
    explicit SensorHub(const Clock& clock) : adc_(clock) {}

    void begin();
    void updatePulse(uint64_t nowUs);  // tickISR (1 ms): 車輪速 x4 + エンジン / クラッチ後回転 (docs/loop_nonblocking_spec.md §6.7)
    void readShutdownSig(uint32_t nowMs);              // tickISR (1 ms): SHUTDOWN 回路出力をデバウンス更新
    // サンプリング ISR (16 kHz): ADC は毎回、IMU は IMU_SAMPLING_DIVIDER 回に 1 回 (8 kHz)。前回の DMA の結果を反映して次を開始する (非ブロッキング)
    void sampleDmaIsr();
    uint32_t adcDmaCount() const { return adc_.dmaCount(); }
    uint32_t imuDmaCount() const { return imu_.dmaCount(); }
    uint32_t imuDmaMissed() const { return imu_.dmaMissed(); }

    // ── const getters (read-only) ──
    const Apps& apps1() const { return apps1_; }
    const Apps& apps2() const { return apps2_; }
    const Tps& tps1() const { return tps1_; }
    const Tps& tps2() const { return tps2_; }
    const Ittr& ittr() const { return ittr_; }
    const Bps& bps() const { return bps_; }
    const EtcTarget& target() const { return target_; }
    const GearPositionSensor& gps() const { return gps_; }
    const ClutchSensor& clutch() const { return clutch_; }
    const Imu& imu() const { return imu_; }
    const Adc& adc() const { return adc_; }
    const RotationSensor& pulseEngine() const { return pulseEngine_; }
    const RotationSensor& pulseClutchRpm() const { return pulseClutchRpm_; }
    const RotationSensor& pulseWheelFL() const { return pulseWheelFL_; }
    const RotationSensor& pulseWheelFR() const { return pulseWheelFR_; }
    const RotationSensor& pulseWheelRL() const { return pulseWheelRL_; }
    const RotationSensor& pulseWheelRR() const { return pulseWheelRR_; }

    // SHUTDOWN 回路出力 (点火系 AND の結果)。tickISR が readShutdownSig() で毎 ms 更新。
    // isOn()==true で ETC 許可。他センサー同様 const 参照で返す。
    const ToggleSwitch& shutdownSig() const { return shutdownSig_; }


    // ── mut: Configurator-only mutable access ──
    struct Mut {
        SensorHub& hub;
        Apps& apps1() { return hub.apps1_; }
        Apps& apps2() { return hub.apps2_; }
        Tps& tps1() { return hub.tps1_; }
        Tps& tps2() { return hub.tps2_; }
        Ittr& ittr() { return hub.ittr_; }
        Bps& bps() { return hub.bps_; }
        EtcTarget& target() { return hub.target_; }
        GearPositionSensor& gps() { return hub.gps_; }
        ClutchSensor& clutch() { return hub.clutch_; }
        // 回転センサーの設定 (config RotationConfig の適用)
        EdgeRotationSensor& pulseEngine() { return hub.pulseEngine_; }
        EdgeRotationSensor& pulseClutchRpm() { return hub.pulseClutchRpm_; }
        EdgeRotationSensor& pulseWheelFL() { return hub.pulseWheelFL_; }
        EdgeRotationSensor& pulseWheelFR() { return hub.pulseWheelFR_; }
        EdgeRotationSensor& pulseWheelRL() { return hub.pulseWheelRL_; }
        EdgeRotationSensor& pulseWheelRR() { return hub.pulseWheelRR_; }
    } mut{*this};

   private:
    Adc adc_;
    Apps apps1_{0, 65535};
    Apps apps2_{0, 65535};
    Tps tps1_{0, 65535};
    Tps tps2_{0, 65535};
    Ittr ittr_;
    Bps bps_;
    EtcTarget target_{apps1_, ittr_};
    GearPositionSensor gps_;
    ClutchSensor clutch_;

    Iam20680hp imu_{IMU_CS_PIN};
    uint8_t imuDivCount_ = 0;  // サンプリング ISR の IMU_SAMPLING_DIVIDER 回に 1 回 IMU

    // 回転センサー: ハードウェア (EdgeSource) + 計算 (EdgeRotationSensor)。
    // 車輪速は FlexPWM (FreqMeasureMulti)、エンジン / クラッチ後は QuadTimer3 (エッジ数 + キャプチャ)
    FreqMeasureEdgeSource wheelFLSource_{PULSE_WHEEL_FL_PIN};
    FreqMeasureEdgeSource wheelFRSource_{PULSE_WHEEL_FR_PIN};
    FreqMeasureEdgeSource wheelRLSource_{PULSE_WHEEL_RL_PIN};
    FreqMeasureEdgeSource wheelRRSource_{PULSE_WHEEL_RR_PIN};
    QuadTimerEdgeSource engineSource_{PULSE_ENGINE_PIN};
    QuadTimerEdgeSource clutchSource_{PULSE_CLUTCH_RPM_PIN};
    EdgeRotationSensor pulseWheelFL_{wheelFLSource_};
    EdgeRotationSensor pulseWheelFR_{wheelFRSource_};
    EdgeRotationSensor pulseWheelRL_{wheelRLSource_};
    EdgeRotationSensor pulseWheelRR_{wheelRRSource_};
    EdgeRotationSensor pulseEngine_{engineSource_};
    EdgeRotationSensor pulseClutchRpm_{clutchSource_};

    // SHUTDOWN 回路出力。アクティブHIGH + 外部 PULLDOWN (素の INPUT)。
    ToggleSwitch shutdownSig_{SHUTDOWN_SIG_IN_PIN, HIGH, ToggleSwitch::InputMode::External};

};
