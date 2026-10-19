#include "sensor/iam20680hp.hpp"

#include "util/log/debug_logger.hpp"

Iam20680hp::Iam20680hp(uint8_t csPin, SPIClass& spi) : spi(spi), csPin(csPin) {}

void Iam20680hp::writeRegister(uint8_t reg, uint8_t val) {
    digitalWriteFast(csPin, LOW);
    spi.beginTransaction(spiSettings);
    spi.transfer(reg & 0x7F);
    spi.transfer(val);
    spi.endTransaction();
    digitalWriteFast(csPin, HIGH);
}

uint8_t Iam20680hp::readRegister(uint8_t reg) {
    digitalWriteFast(csPin, LOW);
    spi.beginTransaction(spiSettings);
    spi.transfer(reg | 0x80);
    uint8_t val = spi.transfer(0x00);
    spi.endTransaction();
    digitalWriteFast(csPin, HIGH);
    return val;
}

void Iam20680hp::begin() {
    spi.begin();
    pinMode(csPin, OUTPUT);
    digitalWriteFast(csPin, HIGH);

    writeRegister(IAM20680_PWR_MGMT_1, 0x80);  // device reset
    delay(100);
    writeRegister(IAM20680_PWR_MGMT_1, 0x01);  // wake (SLEEP=0), CLKSEL=auto(PLL)
    delay(10);
    writeRegister(IAM20680_PWR_MGMT_2, 0x00);  // 全 6 軸有効

    // 最大 ODR 設定:
    //   gyro  : CONFIG.DLPF_CFG=0 → 内部 Fs=8kHz (BW 250Hz)。FCHOICE_B=00 のまま。
    //   accel : ACCEL_CONFIG2.ACCEL_FCHOICE_B=1 (bit3) → DLPF バイパス, Fs=4kHz。
    //   SMPLRT_DIV は Fs=8kHz 時 (DLPF_CFG=0) 無効。
    // 別途ソフトで MovingAverage を掛けてノイズ低減する (オーバーサンプリング平均)。
    writeRegister(IAM20680_SMPLRT_DIV, 0x00);
    writeRegister(IAM20680_CONFIG, 0x00);         // DLPF_CFG=0 → gyro Fs=8kHz
    writeRegister(IAM20680_GYRO_CONFIG, 0x18);    // ±2000dps, FCHOICE_B=00
    writeRegister(IAM20680_ACCEL_CONFIG, 0x18);   // ±16g
    writeRegister(IAM20680_ACCEL_CONFIG2, 0x08);  // ACCEL_FCHOICE_B=1 → accel Fs=4kHz

    // WHO_AM_I を読んでログのみ (期待 ID は実機 datasheet で確定後に検証へ変更)。
    // IMU 未実装でもハングしないよう、ここで停止はしない。
    uint8_t who = readRegister(IAM20680_WHO_AM_I);
    DebugLogger::log("IAM20680HP WHO_AM_I=0x%02X", who);
}

void Iam20680hp::addSample(const uint8_t* buf) {
    int16_t ax = (int16_t)(buf[0] << 8 | buf[1]);
    int16_t ay = (int16_t)(buf[2] << 8 | buf[3]);
    int16_t az = (int16_t)(buf[4] << 8 | buf[5]);
    // buf[6..7] = temperature (未使用)
    int16_t gx = (int16_t)(buf[8] << 8 | buf[9]);
    int16_t gy = (int16_t)(buf[10] << 8 | buf[11]);
    int16_t gz = (int16_t)(buf[12] << 8 | buf[13]);

    accAvg_[0].add(ax * IAM20680_ACC_MG_PER_LSB);
    accAvg_[1].add(ay * IAM20680_ACC_MG_PER_LSB);
    accAvg_[2].add(az * IAM20680_ACC_MG_PER_LSB);
    gyrAvg_[0].add(gx * IAM20680_GYR_DPS_PER_LSB);
    gyrAvg_[1].add(gy * IAM20680_GYR_DPS_PER_LSB);
    gyrAvg_[2].add(gz * IAM20680_GYR_DPS_PER_LSB);
}

Imu::Vec3 Iam20680hp::accel() const {
    return {accAvg_[0].getAvg(), accAvg_[1].getAvg(), accAvg_[2].getAvg()};
}

Imu::Vec3 Iam20680hp::gyro() const {
    return {gyrAvg_[0].getAvg(), gyrAvg_[1].getAvg(), gyrAvg_[2].getAvg()};
}

void Iam20680hp::beginDma() {
    // 送信: 1 バイト目 = 読み始めのアドレス (read ビット付き)、残りはダミー。ワード内は MSB から送られる
    txWords_[0] = (uint32_t)(IAM20680_ACCEL_XOUT_H | 0x80) << 24;
    txWords_[1] = 0;
    txWords_[2] = 0;
    txWords_[3] = 0;

    // CS をハードウェア (LPSPI3 PCS0) に切り替え、120 bit = 1 フレームにする。トランザクションは開いたまま (SPI1 は IMU 専用)
    spi.setCS(csPin);
    spi.beginTransaction(spiSettings);
    IMXRT_LPSPI3_S.TCR = (IMXRT_LPSPI3_S.TCR & ~(LPSPI_TCR_FRAMESZ(0xFFF) | LPSPI_TCR_PCS(3))) |
                         LPSPI_TCR_FRAMESZ(IAM20680_READ_BYTES * 8 - 1) | LPSPI_TCR_PCS(0);
    IMXRT_LPSPI3_S.FCR = LPSPI_FCR_RXWATER(0) | LPSPI_FCR_TXWATER(0);
    IMXRT_LPSPI3_S.DER = LPSPI_DER_TDDE | LPSPI_DER_RDDE;

    rxDma_.disable();
    rxDma_.source(*(volatile uint32_t*)&IMXRT_LPSPI3_S.RDR);
    rxDma_.destinationBuffer((uint32_t*)rxWords_, sizeof(rxWords_));
    rxDma_.triggerAtHardwareEvent(DMAMUX_SOURCE_LPSPI3_RX);
    rxDma_.disableOnCompletion();

    txDma_.disable();
    txDma_.sourceBuffer((uint32_t*)txWords_, sizeof(txWords_));
    txDma_.destination(*(volatile uint32_t*)&IMXRT_LPSPI3_S.TDR);
    txDma_.triggerAtHardwareEvent(DMAMUX_SOURCE_LPSPI3_TX);
    txDma_.disableOnCompletion();
}

void Iam20680hp::sampleDmaIsr() {
    if (dmaStarted_) {
        if (!rxDma_.complete()) {
            dmaMissed_ = dmaMissed_ + 1;  // まだ転送中: 取り込みも次の開始もしない
            return;
        }
        rxDma_.clearComplete();
        // 受信の 4 ワードを 15 バイトに戻す (最後のワードは下位 24 bit)。1 バイト目はアドレスを送っている間の不定値
        const uint32_t w0 = rxWords_[0], w1 = rxWords_[1], w2 = rxWords_[2], w3 = rxWords_[3];
        const uint8_t bytes[IAM20680_READ_BYTES] = {
            (uint8_t)(w0 >> 24), (uint8_t)(w0 >> 16), (uint8_t)(w0 >> 8), (uint8_t)w0,
            (uint8_t)(w1 >> 24), (uint8_t)(w1 >> 16), (uint8_t)(w1 >> 8), (uint8_t)w1,
            (uint8_t)(w2 >> 24), (uint8_t)(w2 >> 16), (uint8_t)(w2 >> 8), (uint8_t)w2,
            (uint8_t)(w3 >> 16), (uint8_t)(w3 >> 8), (uint8_t)w3,
        };
        addSample(&bytes[1]);
        dmaCount_ = dmaCount_ + 1;
    }
    // 次の転送を開始 (disableOnCompletion で止まっているので、位置を戻して受信 → 送信の順に有効にする)
    rxDma_.destinationBuffer((uint32_t*)rxWords_, sizeof(rxWords_));
    txDma_.sourceBuffer((uint32_t*)txWords_, sizeof(txWords_));
    rxDma_.enable();
    txDma_.enable();
    dmaStarted_ = true;
}
