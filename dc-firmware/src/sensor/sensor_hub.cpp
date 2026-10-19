#include "sensor_hub.hpp"

void SensorHub::begin() {
    adc_.begin();
    imu_.begin();
    imu_.beginDma();
    pulseWheelFL_.begin();
    pulseWheelFR_.begin();
    pulseWheelRL_.begin();
    pulseWheelRR_.begin();
    pulseEngine_.begin();
    pulseClutchRpm_.begin();
    shutdownSig_.initialize();
}

// IMU の移動平均の窓が 1 ms 分であること (IMU の読む周期 × 窓 = 1000 µs)
static_assert(SENSOR_SAMPLING_RATE_US * IMU_SAMPLING_DIVIDER * IAM20680_AVG_WINDOW == 1000.0,
              "IMU_SAMPLING_DIVIDER を変えたら IAM20680_AVG_WINDOW を 1 ms 分に合わせること");

void SensorHub::sampleDmaIsr() {
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
    if (++imuDivCount_ >= IMU_SAMPLING_DIVIDER) {
        imuDivCount_ = 0;
        imu_.sampleDmaIsr();  // 16 kHz / IMU_SAMPLING_DIVIDER = 8 kHz
    }
}

void SensorHub::updatePulse(uint64_t nowUs) {
    pulseWheelFL_.update(nowUs);
    pulseWheelFR_.update(nowUs);
    pulseWheelRL_.update(nowUs);
    pulseWheelRR_.update(nowUs);
    pulseEngine_.update(nowUs);
    pulseClutchRpm_.update(nowUs);
}

void SensorHub::readShutdownSig(uint32_t nowMs) {
    shutdownSig_.read(nowMs);
}
