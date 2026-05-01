#ifndef _ICM45686_H_
#define _ICM45686_H_

#include "imu.hpp"

#define ICM45686_ACCEL_DATA_X1 0x00
#define ICM45686_GYRO_DATA_X1 0x06
#define ICM45686_PWR_MGMT0 0x10
#define ICM45686_ACCEL_CONFIG0 0x1B
#define ICM45686_GYRO_CONFIG0 0x1C
#define ICM45686_WHO_AM_I 0x72
#define ICM45686_WHO_AM_I_VAL 0xE9
#define ICM45686_REG_MISC2 0x7F

#define ICM45686_SPI_FREQUENCY 12000000

#define ICM45686_ACC_SENSITIVITY_16G (16000.0f / 32768.0f)
#define ICM45686_GYR_SENSITIVITY_2000 (2000.0f / 32768.0f)

class Icm45686 : public Imu
{
public:
    Icm45686(uint8_t csPin, SPIClass &spi = SPI1);
    void begin() override;
    void read() override;

private:
    SPIClass &spi;
    uint8_t csPin;
    SPISettings spiSettings = SPISettings(ICM45686_SPI_FREQUENCY, MSBFIRST, SPI_MODE3);

    void writeRegister(uint8_t reg, uint8_t val);
    uint8_t readRegister(uint8_t reg);
    void readRegisters(uint8_t reg, uint8_t *buf, uint8_t len);
};

#endif // _ICM45686_H_
