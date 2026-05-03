#pragma once

#include "adc.hpp"
#include "constants.hpp"
#include "icm45686.hpp"
#include "pulse_counter.hpp"
#include "sensors.hpp"

class SensorHub {
   public:
    void begin();
    void read();         // ISR (8kHz): adc + sensors + imu
    void updatePulse();  // loop: PulseCounter x5

    // ── const getters (read-only) ──
    const Apps& apps1() const { return apps1_; }
    const Apps& apps2() const { return apps2_; }
    const Tps& tps1() const { return tps1_; }
    const Tps& tps2() const { return tps2_; }
    const Ittr& ittr() const { return ittr_; }
    const Bps& bps() const { return bps_; }
    const EtcTarget& target() const { return target_; }
    const Imu* imu() const { return imu_; }
    float wheelSpeedFL() const { return pulseWheelFL_.getFrequencyHz(); }
    float wheelSpeedFR() const { return pulseWheelFR_.getFrequencyHz(); }
    float wheelSpeedRL() const { return pulseWheelRL_.getFrequencyHz(); }
    float wheelSpeedRR() const { return pulseWheelRR_.getFrequencyHz(); }
    float engineRpm() const { return pulseEngine_.getFrequencyHz(); }
    uint32_t sps() const { return adc_.sps(); }

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
    } mut{*this};

   private:
    Adc adc_;
    Apps apps1_{APPS_1_RAW_MIN, APPS_1_RAW_MAX};
    Apps apps2_{APPS_2_RAW_MIN, APPS_2_RAW_MAX};
    Tps tps1_{TPS_1_RAW_MIN, TPS_1_RAW_MAX};
    Tps tps2_{TPS_2_RAW_MIN, TPS_2_RAW_MAX};
    Ittr ittr_;
    Bps bps_;
    EtcTarget target_{apps1_, ittr_};

    Icm45686 imu_impl_{IMU_CS_PIN};
    Imu* imu_ = &imu_impl_;

    PulseCounter pulseWheelFL_{PULSE_WHEEL_FL_PIN};
    PulseCounter pulseWheelFR_{PULSE_WHEEL_FR_PIN};
    PulseCounter pulseWheelRL_{PULSE_WHEEL_RL_PIN};
    PulseCounter pulseWheelRR_{PULSE_WHEEL_RR_PIN};
    PulseCounter pulseEngine_{PULSE_ENGINE_PIN};
};

