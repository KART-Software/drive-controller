#include "adc.hpp"

// LPSPI RX 待ちの最大スピン時間 (us)。ADC 無応答時に 8kHz ISR (NVIC 優先度16) が
// 固まって USB 等の低優先割り込みを枯渇させるのを防ぐ。
// read() は 1 回の ISR で transferCommand を 8 回呼ぶため、最悪 8×timeout が
// ISR 周期 125us を超えないこと。1 フレーム転送は ~3us (@10MHz) なので 10us で十分。
static constexpr uint32_t ADC_SPI_TIMEOUT_US = 10;

template <size_t NUM_DEV>
_adc<NUM_DEV>::_adc(uint8_t csPin, SPIClass& spi) : spi(spi), csPin(csPin) {
    for (size_t i = 0; i < NUM_DEV * ADC_NUM_CH; ++i) {
        value[i] = 0;
    }
}

// デイジーチェーン+HWCS対応 _adc<NUM_DEV> テンプレート実装
template <size_t NUM_DEV>
void _adc<NUM_DEV>::begin() {
    spi.begin();
    *(portConfigRegister(csPin)) = 3;

    // 全チャンネル(0-7)を有効化
    for (uint8_t ch = 0; ch < ADC_NUM_CH; ++ch) {
        writeRegister(RANGE_SELECT_ADDR_0 + ch, RANGE_4);
    }
    writeRegister(CH_POWER_DOWN_ADDR, 0x00);  // 全ch有効
    writeRegister(AUTO_SEQ_EN_ADDR, 0xFF);    // 全ch自動シーケンス
    uint16_t dummy[NUM_DEV];
    transferCommand(AUTO_RST, dummy);  // 初期化用ダミー
}

template <size_t NUM_DEV>
void _adc<NUM_DEV>::writeRegister(uint8_t addr, uint8_t value) {
    constexpr uint32_t frameBits = NUM_DEV * 24;
    uint64_t tx = 0;

    for (size_t dev = 0; dev < NUM_DEV; ++dev) {
        tx = (tx << 24) | ((uint32_t)((addr << 1) | 0x01) << 16) | ((uint32_t)value << 8);
    }

    spi.beginTransaction(spiSettings);
    IMXRT_LPSPI4_S.TCR = (IMXRT_LPSPI4_S.TCR & 0xFFFFF000) | LPSPI_TCR_FRAMESZ(frameBits - 1);
    if (frameBits > 32) {
        IMXRT_LPSPI4_S.TDR = (uint32_t)(tx >> 32);
    }
    IMXRT_LPSPI4_S.TDR = (uint32_t)(tx & 0xFFFFFFFF);
    uint32_t t0 = micros();
    while (IMXRT_LPSPI4_S.RSR & LPSPI_RSR_RXEMPTY) {
        if ((uint32_t)(micros() - t0) > ADC_SPI_TIMEOUT_US)
            break;
    }
    (void)IMXRT_LPSPI4_S.RDR;
    spi.endTransaction();
    delayMicroseconds(1);
}

template <size_t NUM_DEV>
void _adc<NUM_DEV>::transferCommand(uint16_t cmd, uint16_t* out) {
    constexpr uint32_t frameBits = (1 + NUM_DEV) * 16;
    uint64_t tx = (uint64_t)cmd << (NUM_DEV * 16);

    spi.beginTransaction(spiSettings);
    IMXRT_LPSPI4_S.TCR = (IMXRT_LPSPI4_S.TCR & 0xFFFFF000) | LPSPI_TCR_FRAMESZ(frameBits - 1);
    if (frameBits > 32) {
        IMXRT_LPSPI4_S.TDR = (uint32_t)(tx >> 32);
    }
    IMXRT_LPSPI4_S.TDR = (uint32_t)(tx & 0xFFFFFFFF);
    uint32_t t0 = micros();
    while (IMXRT_LPSPI4_S.RSR & LPSPI_RSR_RXEMPTY) {
        if ((uint32_t)(micros() - t0) > ADC_SPI_TIMEOUT_US)
            break;
    }
    uint64_t rx = 0;
    if (frameBits > 32) {
        rx = (uint64_t)IMXRT_LPSPI4_S.RDR << 32;
    }
    rx |= IMXRT_LPSPI4_S.RDR;
    spi.endTransaction();
    delayMicroseconds(1);

    for (size_t dev = 0; dev < NUM_DEV; ++dev) {
        out[dev] = (rx >> (16 * (NUM_DEV - 1 - dev))) & 0xFFFF;
    }
}

template <size_t NUM_DEV>
void _adc<NUM_DEV>::read() {
    freqMeter_.tick();
    // 全デバイス・全チャンネルを順次取得
    for (uint8_t seq = 0; seq < ADC_NUM_CH; ++seq) {
        uint16_t data[NUM_DEV];
        transferCommand(seq == ADC_NUM_CH - 1 ? AUTO_RST : NO_OP, data);
        for (size_t dev = 0; dev < NUM_DEV; ++dev) {
            value[dev * ADC_NUM_CH + seq] = data[dev];
        }
    }
}

template class _adc<1>;
template class _adc<2>;
