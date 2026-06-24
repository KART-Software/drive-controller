#pragma once

#include "adc.hpp"
#include "constants.hpp"
#include "iam20680hp.hpp"
#include "pulse_counter.hpp"
#include "sensors.hpp"
#include "util/toggle_switch.hpp"
#include "wheel_speed.hpp"

class SensorHub {
   public:
    void begin();
    void read();         // 非DMA: adc(ブロッキング) + sensors + imu (loop から)
    void updatePulse();  // loop: PulseCounter x5
#ifdef ADC_DMA
    void sampleAdcDmaIsr();  // 8kHz ISR: 前回 DMA 結果を averages に反映し次を kick (非ブロッキング)
    void readImu();          // loop: IMU のみ (DMA 経路では adc と分離)
    uint32_t adcDmaCount() const { return adc_.dmaCount(); }
#endif

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
    const Imu* imu() const { return imu_; }
    const Adc& adc() const { return adc_; }
    const PulseCounter& pulseEngine() const { return pulseEngine_; }
    const PulseCounter& pulseClutchRpm() const { return pulseClutchRpm_; }
    const WheelSpeedSensor& pulseWheelFL() const { return pulseWheelFL_; }
    const WheelSpeedSensor& pulseWheelFR() const { return pulseWheelFR_; }
    const WheelSpeedSensor& pulseWheelRL() const { return pulseWheelRL_; }
    const WheelSpeedSensor& pulseWheelRR() const { return pulseWheelRR_; }

    // SHUTDOWN 回路出力 (点火系 AND の結果)。read()/readImu() で毎ループ更新。
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

    Iam20680hp imu_impl_{IMU_CS_PIN};
    Imu* imu_ = &imu_impl_;

    // 車輪速は FlexPWM (FreqMeasureMulti)、Engine/クラッチ後RPM は QuadTimer (PulseCounter)
    WheelSpeedSensor pulseWheelFL_{PULSE_WHEEL_FL_PIN};
    WheelSpeedSensor pulseWheelFR_{PULSE_WHEEL_FR_PIN};
    WheelSpeedSensor pulseWheelRL_{PULSE_WHEEL_RL_PIN};
    WheelSpeedSensor pulseWheelRR_{PULSE_WHEEL_RR_PIN};
    PulseCounter pulseEngine_{PULSE_ENGINE_PIN};
    PulseCounter pulseClutchRpm_{PULSE_CLUTCH_RPM_PIN};

    // SHUTDOWN 回路出力。アクティブHIGH + 外部 PULLDOWN (素の INPUT)。
    ToggleSwitch shutdownSig_{SHUTDOWN_SIG_IN_PIN, HIGH, ToggleSwitch::InputMode::External};
};
