#pragma once

#include "adc.hpp"
#include "constants.hpp"
#include "iam20680hp.hpp"
#include "edge_rotation_sensor.hpp"
#include "edge_wheel_speed_sensor.hpp"
#include "freqmeasure_edge_source.hpp"
#include "interfaces/sensor/rotation_sensor.hpp"
#include "quadtimer_edge_source.hpp"
#include "sensor_view.hpp"
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

    // 見るだけの口 (const 参照の束。見るだけの部品にはこれを渡す)
    const SensorView& view() const { return view_; }

    // ── 書き換え可の参照 (SensorHub を持つ main.cpp と Configurator だけが使う: 校正の適用、モード反映、設定) ──
    Apps& apps1() { return apps1_; }
    Apps& apps2() { return apps2_; }
    Tps& tps1() { return tps1_; }
    Tps& tps2() { return tps2_; }
    Ittr& ittr() { return ittr_; }
    Bps& bps() { return bps_; }
    EtcTarget& target() { return target_; }
    GearPositionSensor& gps() { return gps_; }
    ClutchSensor& clutch() { return clutch_; }
    // 回転センサーの設定 (config RotationConfig の適用)
    EdgeRotationSensor& pulseEngine() { return pulseEngine_; }
    EdgeRotationSensor& pulseClutchRpm() { return pulseClutchRpm_; }
    EdgeWheelSpeedSensor& pulseWheelFL() { return pulseWheelFL_; }
    EdgeWheelSpeedSensor& pulseWheelFR() { return pulseWheelFR_; }
    EdgeWheelSpeedSensor& pulseWheelRL() { return pulseWheelRL_; }
    EdgeWheelSpeedSensor& pulseWheelRR() { return pulseWheelRR_; }

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

    // 回転センサー: ハードウェア (EdgeSource) + 計算 (EdgeRotationSensor)。車輪はタイヤ直径を持つ EdgeWheelSpeedSensor (車速も出す)。
    // 車輪速は FlexPWM (FreqMeasureMulti)、エンジン / クラッチ後は QuadTimer3 (エッジ数 + キャプチャ)
    FreqMeasureEdgeSource wheelFLSource_{PULSE_WHEEL_FL_PIN};
    FreqMeasureEdgeSource wheelFRSource_{PULSE_WHEEL_FR_PIN};
    FreqMeasureEdgeSource wheelRLSource_{PULSE_WHEEL_RL_PIN};
    FreqMeasureEdgeSource wheelRRSource_{PULSE_WHEEL_RR_PIN};
    QuadTimerEdgeSource engineSource_{PULSE_ENGINE_PIN};
    QuadTimerEdgeSource clutchSource_{PULSE_CLUTCH_RPM_PIN};
    EdgeWheelSpeedSensor pulseWheelFL_{wheelFLSource_};
    EdgeWheelSpeedSensor pulseWheelFR_{wheelFRSource_};
    EdgeWheelSpeedSensor pulseWheelRL_{wheelRLSource_};
    EdgeWheelSpeedSensor pulseWheelRR_{wheelRRSource_};
    EdgeRotationSensor pulseEngine_{engineSource_};
    EdgeRotationSensor pulseClutchRpm_{clutchSource_};

    // SHUTDOWN 回路出力。アクティブHIGH + 外部 PULLDOWN (素の INPUT)。
    ToggleSwitch shutdownSig_{SHUTDOWN_SIG_IN_PIN, HIGH, ToggleSwitch::InputMode::External};

    // 見るだけの口。メンバの最後 (参照先の全センサーより後) に置く
    SensorView view_{SensorView::Refs{
        .apps1 = apps1_,
        .apps2 = apps2_,
        .tps1 = tps1_,
        .tps2 = tps2_,
        .ittr = ittr_,
        .bps = bps_,
        .target = target_,
        .gps = gps_,
        .clutch = clutch_,
        .imu = imu_,
        .adc = adc_,
        .pulseEngine = pulseEngine_,
        .pulseClutchRpm = pulseClutchRpm_,
        .pulseWheelFL = pulseWheelFL_,
        .pulseWheelFR = pulseWheelFR_,
        .pulseWheelRL = pulseWheelRL_,
        .pulseWheelRR = pulseWheelRR_,
        .shutdownSig = shutdownSig_,
    }};

};
