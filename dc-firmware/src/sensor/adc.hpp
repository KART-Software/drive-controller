#pragma once

#include <Arduino.h>
#include <SPI.h>
#include "constants.hpp"
#include "sensor/frequency_meter.hpp"

// https://www.ti.com/jp/lit/ds/symlink/ads8688.pdf

#define SPI_MODE_ADC SPI_MODE1
#define SPI_BIT_ORDER MSBFIRST
#define SPI_FREQUENCY 10000000

#define AUTO_SEQ_EN_ADDR 0x01
#define CH_POWER_DOWN_ADDR 0x02

/// Registers
#define NO_OP 0x0000
#define AUTO_RST 0xA000

/// Range Select

#define RANGE_SELECT_ADDR_0 0x05
#define RANGE_SELECT_ADDR_1 0x06
#define RANGE_SELECT_ADDR_2 0x07
#define RANGE_SELECT_ADDR_3 0x08
#define RANGE_SELECT_ADDR_4 0x09
#define RANGE_SELECT_ADDR_5 0x0A
#define RANGE_SELECT_ADDR_6 0x0B
#define RANGE_SELECT_ADDR_7 0x0C

#define RANGE_0 0b0000  // ±2.5 x VREF
#define RANGE_1 0b0001  // ±1.25 x VREF
#define RANGE_2 0b0010  // ±0.625 x VREF
#define RANGE_3 0b0101  // 0 ~ 2.5 x VREF
#define RANGE_4 \
    0b0110  // 0 ~ 1.25 x VREF
            // VREF = 4.096V

#define ADC_NUM_CH 8

// デイジーチェーン対応 ADS8688
template <size_t NUM_DEV>
class _adc {
   public:
    _adc(uint8_t csPin = ADC_CS_PIN, SPIClass& spi = SPI);

    void begin();
    void read();
    uint16_t value[NUM_DEV * ADC_NUM_CH] = {};
    const uint16_t* deviceValue(size_t device) const { return &value[device * ADC_NUM_CH]; }
    uint32_t sps() const { return freqMeter_.hz(); }

   private:
    SPIClass& spi;
    uint8_t csPin;
    SPISettings spiSettings = SPISettings(SPI_FREQUENCY, SPI_BIT_ORDER, SPI_MODE_ADC);
    FrequencyMeter freqMeter_;

    void writeRegister(uint8_t addr, uint8_t value);
    void transferCommand(uint16_t cmd, uint16_t* out);
};

// TODO[bench]: 2 台目の ADS8688 を接続したら _adc<2> に戻す。
// 現在はベンチで 1 台のみ接続のため _adc<1>。
using Adc = _adc<1>;

