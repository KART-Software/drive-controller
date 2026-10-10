#include "sensor_hub.hpp"

void SensorHub::begin() {
    adc_.begin();
    imu_impl_.begin();
    pulseWheelFL_.begin();
    pulseWheelFR_.begin();
    pulseWheelRL_.begin();
    pulseWheelRR_.begin();
    pulseEngine_.begin();
    pulseClutchRpm_.begin();
    shutdownSig_.initialize();
}

void SensorHub::read() {
    adc_.read();
    apps1_.update(adc_.value[APPS_1_CH]);
    apps2_.update(adc_.value[APPS_2_CH]);
    ittr_.update(adc_.value[ITTR_CH]);
    tps1_.update(adc_.value[TPS_1_CH]);
    tps2_.update(adc_.value[TPS_2_CH]);
    bps_.update(adc_.value[BPS_CH]);
    gps_.update(adc_.value[GPS_CH]);
    clutch_.update(adc_.value[CLUTCH_CH]);
    imu_impl_.read();
    shutdownSig_.read();  // SHUTDOWN 回路出力をデバウンス更新 (毎ループ)
}

#ifdef ADC_DMA
void SensorHub::sampleAdcDmaIsr() {
    adc_.latchDma();  // 前回 kick した DMA の結果 (value[]) を確定
    apps1_.update(adc_.value[APPS_1_CH]);
    apps2_.update(adc_.value[APPS_2_CH]);
    ittr_.update(adc_.value[ITTR_CH]);
    tps1_.update(adc_.value[TPS_1_CH]);
    tps2_.update(adc_.value[TPS_2_CH]);
    bps_.update(adc_.value[BPS_CH]);
    gps_.update(adc_.value[GPS_CH]);
    clutch_.update(adc_.value[CLUTCH_CH]);
    adc_.startDma();  // 次の 8ch 転送を kick (非ブロッキング)
}

void SensorHub::readImu() {
    imu_impl_.read();
    shutdownSig_.read();  // SHUTDOWN 回路出力をデバウンス更新 (毎ループ)
}
#endif

void SensorHub::updatePulse() {
    pulseWheelFL_.update();
    pulseWheelFR_.update();
    pulseWheelRL_.update();
    pulseWheelRR_.update();
    pulseEngine_.update();
    pulseClutchRpm_.update();
}
