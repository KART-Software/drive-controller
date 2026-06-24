#pragma once

#include "imu.hpp"
#include "util/moving_average.hpp"

// IAM-20680HP (TDK InvenSense, 車載 6 軸)。MPU-6500 / ICM-20680 系のレジスタ互換。
// 旧 ICM-45686 とはレジスタ・感度・ODR 設定方法が全く異なる別系統。
// https://product.tdk.com/system/files/dam/doc/product/sensor/mortion-inertial/imu/data_sheet/ds-000409-iam-20680hp.pdf

// ── レジスタ (MPU-6500 ファミリ) ──
#define IAM20680_SMPLRT_DIV 0x19
#define IAM20680_CONFIG 0x1A
#define IAM20680_GYRO_CONFIG 0x1B
#define IAM20680_ACCEL_CONFIG 0x1C
#define IAM20680_ACCEL_CONFIG2 0x1D
#define IAM20680_ACCEL_XOUT_H 0x3B  // accel(6) + temp(2) + gyro(6) が連続
#define IAM20680_PWR_MGMT_1 0x6B
#define IAM20680_PWR_MGMT_2 0x6C
#define IAM20680_WHO_AM_I 0x75

#define IAM20680_SPI_FREQUENCY 8000000  // datasheet 上限 8MHz (全レジスタ)。ICM45686 は 12MHz だった

// 感度: ±2000dps → 16.4 LSB/dps、±16g → 2048 LSB/g。Imu は accel=mg, gyro=dps。
#define IAM20680_GYR_DPS_PER_LSB (1.0f / 16.4f)
#define IAM20680_ACC_MG_PER_LSB (1000.0f / 2048.0f)

// オーバーサンプリング平均の窓 (loop 読み単位)。ノイズは概ね 1/sqrt(N)。
#define IAM20680_AVG_WINDOW 16

class Iam20680hp : public Imu {
   public:
    Iam20680hp(uint8_t csPin, SPIClass& spi = SPI1);
    void begin() override;
    void read() override;  // loop から毎回。read 毎に平均へ add し、accel[]/gyro[] は平均値

   private:
    SPIClass& spi;
    uint8_t csPin;
    SPISettings spiSettings = SPISettings(IAM20680_SPI_FREQUENCY, MSBFIRST, SPI_MODE3);
    MovingAverage<IAM20680_AVG_WINDOW> accAvg_[3];
    MovingAverage<IAM20680_AVG_WINDOW> gyrAvg_[3];

    void writeRegister(uint8_t reg, uint8_t val);
    uint8_t readRegister(uint8_t reg);
    void readRegisters(uint8_t reg, uint8_t* buf, uint8_t len);
};
