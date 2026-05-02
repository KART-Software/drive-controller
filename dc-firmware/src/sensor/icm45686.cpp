#include "sensor/icm45686.hpp"

Icm45686::Icm45686(uint8_t csPin, SPIClass& spi) : spi(spi), csPin(csPin) {}

void Icm45686::writeRegister(uint8_t reg, uint8_t val) {
    digitalWriteFast(csPin, LOW);
    spi.beginTransaction(spiSettings);
    spi.transfer(reg & 0x7F);
    spi.transfer(val);
    spi.endTransaction();
    digitalWriteFast(csPin, HIGH);
}

uint8_t Icm45686::readRegister(uint8_t reg) {
    digitalWriteFast(csPin, LOW);
    spi.beginTransaction(spiSettings);
    spi.transfer(reg | 0x80);
    uint8_t val = spi.transfer(0x00);
    spi.endTransaction();
    digitalWriteFast(csPin, HIGH);
    return val;
}

void Icm45686::readRegisters(uint8_t reg, uint8_t* buf, uint8_t len) {
    digitalWriteFast(csPin, LOW);
    spi.beginTransaction(spiSettings);
    spi.transfer(reg | 0x80);
    for (uint8_t i = 0; i < len; i++) {
        buf[i] = spi.transfer(0x00);
    }
    spi.endTransaction();
    digitalWriteFast(csPin, HIGH);
}

void Icm45686::begin() {
    spi.begin();
    pinMode(csPin, OUTPUT);
    digitalWriteFast(csPin, HIGH);
    delay(1);

    writeRegister(ICM45686_REG_MISC2, 0x02);  // soft reset
    delay(10);

    (void)readRegister(ICM45686_WHO_AM_I);

    // ±16g, 6.4kHz ODR in low-noise mode.
    writeRegister(ICM45686_ACCEL_CONFIG0, 0x13);

    // ±2000dps, 6.4kHz ODR in low-noise mode.
    writeRegister(ICM45686_GYRO_CONFIG0, 0x13);

    // GYRO_MODE=LN, ACCEL_MODE=LN.
    writeRegister(ICM45686_PWR_MGMT0, 0x0F);
    delay(50);
}

void Icm45686::read() {
    uint8_t buf[12];
    readRegisters(ICM45686_ACCEL_DATA_X1, buf, sizeof(buf));

    int16_t ax = (int16_t)(buf[0] << 8 | buf[1]);
    int16_t ay = (int16_t)(buf[2] << 8 | buf[3]);
    int16_t az = (int16_t)(buf[4] << 8 | buf[5]);
    int16_t gx = (int16_t)(buf[6] << 8 | buf[7]);
    int16_t gy = (int16_t)(buf[8] << 8 | buf[9]);
    int16_t gz = (int16_t)(buf[10] << 8 | buf[11]);

    accel[0] = ax * ICM45686_ACC_SENSITIVITY_16G;
    accel[1] = ay * ICM45686_ACC_SENSITIVITY_16G;
    accel[2] = az * ICM45686_ACC_SENSITIVITY_16G;
    gyro[0] = gx * ICM45686_GYR_SENSITIVITY_2000;
    gyro[1] = gy * ICM45686_GYR_SENSITIVITY_2000;
    gyro[2] = gz * ICM45686_GYR_SENSITIVITY_2000;
}
