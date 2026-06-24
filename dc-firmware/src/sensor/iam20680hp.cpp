#include "sensor/iam20680hp.hpp"

#include "serial/serial_protocol.hpp"

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

void Iam20680hp::readRegisters(uint8_t reg, uint8_t* buf, uint8_t len) {
    digitalWriteFast(csPin, LOW);
    spi.beginTransaction(spiSettings);
    spi.transfer(reg | 0x80);  // レジスタアドレス (read bit)
    spi.transfer(buf, len);    // ブロック転送 (read なので送信内容は don't-care)
    spi.endTransaction();
    digitalWriteFast(csPin, HIGH);
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
    SerialProtocol::sendDebugf("IAM20680HP WHO_AM_I=0x%02X", who);
}

void Iam20680hp::read() {
    uint8_t buf[14];  // accel(6) + temp(2) + gyro(6)
    readRegisters(IAM20680_ACCEL_XOUT_H, buf, sizeof(buf));

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

    for (int i = 0; i < 3; i++) {
        accel[i] = accAvg_[i].getAvg();  // 移動平均値を公開 (ノイズ低減後)
        gyro[i] = gyrAvg_[i].getAvg();
    }
}
