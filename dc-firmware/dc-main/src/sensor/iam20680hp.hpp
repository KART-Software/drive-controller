#pragma once

#include <Arduino.h>
#include <DMAChannel.h>
#include <SPI.h>

#include "interfaces/sensor/imu.hpp"
#include "util/moving_average.hpp"

// IAM-20680HP (TDK InvenSense, 車載 6 軸)。MPU-6500 / ICM-20680 系のレジスタ互換。
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

#define IAM20680_SPI_FREQUENCY 8000000  // datasheet の上限

// 感度: ±2000dps → 16.4 LSB/dps、±16g → 2048 LSB/g。Imu は accel=mg, gyro=dps。
#define IAM20680_GYR_DPS_PER_LSB (1.0f / 16.4f)
#define IAM20680_ACC_MG_PER_LSB (1000.0f / 2048.0f)

// オーバーサンプリング平均の窓 (サンプル数)。DMA では 8 kHz × 8 = 1 ms 分 (読む周期は IMU_SAMPLING_DIVIDER)。ノイズは概ね 1/sqrt(N)。
#define IAM20680_AVG_WINDOW 8

// 1 回の読み出し: アドレス 1 バイト + accel(6) + temp(2) + gyro(6) = 15 バイト = 120 bit
#define IAM20680_READ_BYTES 15

// - begin(): setup でブロッキングにレジスタを設定する (CS はソフトウェア)。
// - 読み出しは DMA (docs/loop_nonblocking_spec.md §6.4): beginDma() の後、サンプリング ISR (16 kHz) の
//   IMU_SAMPLING_DIVIDER 回に 1 回 (= 8 kHz) から sampleDmaIsr() を呼ぶ。前回の DMA の結果を移動平均に入れ、次の 15 バイトの転送を開始するだけ
//   (非ブロッキング)。CS はハードウェア (LPSPI3 PCS0 = pin 0) が 120 bit の 1 フレームの間 Low に保つ。
//   送るのは毎回同じ (アドレス + ダミー) なので、送信の内容は beginDma() で一度だけ用意する。
// - SPI1 は IMU 専用になる: beginDma() はトランザクションを開いたまま (endTransaction しない) にし、CS をハードウェアに
//   切り替える。以後 SPI1 にほかのデバイスはつなげず、begin() (ブロッキング、CS はソフトウェア) も呼べない。
class Iam20680hp : public Imu {
   public:
    Iam20680hp(uint8_t csPin, SPIClass& spi = SPI1);
    void begin() override;

    Vec3 accel() const override;
    Vec3 gyro() const override;

    void beginDma();      // setup で begin() の後に一度だけ
    void sampleDmaIsr();  // サンプリング ISR から (8 kHz)
    uint32_t dmaCount() const { return dmaCount_; }
    uint32_t dmaMissed() const { return dmaMissed_; }  // 前回の転送が終わっていなかった回数

   private:
    void addSample(const uint8_t* data);  // accel(6) + temp(2) + gyro(6) の 14 バイトを移動平均に入れる

    SPIClass& spi;
    uint8_t csPin;
    SPISettings spiSettings = SPISettings(IAM20680_SPI_FREQUENCY, MSBFIRST, SPI_MODE3);
    MovingAverage<IAM20680_AVG_WINDOW> accAvg_[3];
    MovingAverage<IAM20680_AVG_WINDOW> gyrAvg_[3];

    void writeRegister(uint8_t reg, uint8_t val);
    uint8_t readRegister(uint8_t reg);

    DMAChannel txDma_;
    DMAChannel rxDma_;
    // 120 bit のフレームは 32 + 32 + 32 + 24 bit の 4 ワードで FIFO を通る (RM LPSPI TCR[FRAMESZ])
    volatile uint32_t txWords_[4];
    volatile uint32_t rxWords_[4];
    bool dmaStarted_ = false;
    volatile uint32_t dmaCount_ = 0;
    volatile uint32_t dmaMissed_ = 0;
};
