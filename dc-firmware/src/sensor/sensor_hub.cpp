#include "sensor_hub.hpp"

void SensorHub::begin() {
    adc_.begin();
    imu_impl_.begin();
    pulseWheelFL_.begin();
    pulseWheelFR_.begin();
    pulseWheelRL_.begin();
    pulseWheelRR_.begin();
    pulseEngine_.begin();
}

void SensorHub::read() {
    adc_.read();
    apps1_.update(adc_.value[APPS_1_CH]);
    apps2_.update(adc_.value[APPS_2_CH]);
    ittr_.update(adc_.value[ITTR_CH]);
    tps1_.update(adc_.value[TPS_1_CH]);
    tps2_.update(adc_.value[TPS_2_CH]);
    bps_.update(adc_.value[BPS_CH]);
    imu_impl_.read();
}

void SensorHub::updatePulse() {
    pulseWheelFL_.update();
    pulseWheelFR_.update();
    pulseWheelRL_.update();
    pulseWheelRR_.update();
    pulseEngine_.update();
}
