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
#ifdef ADC_DMA
    beginDma();
#endif
}

#ifdef ADC_DMA
// DMA 経路: 8kHz ISR から startDma() を kick し、while ビジーウェイトなしで 8ch を取得。
// CS フレーミングは非DMA経路と同じく 32bit フレーム/フレーム毎 (CONT=0)。
template <size_t NUM_DEV>
void _adc<NUM_DEV>::beginDma() {
    // 送信コマンド列: ch0..6=NO_OP, 末尾=AUTO_RST (ブロッキング read() と同じ並び)
    for (size_t i = 0; i < NUM_DEV * ADC_NUM_CH; ++i)
        txCmds_[i] = (uint32_t)NO_OP << 16;
    txCmds_[NUM_DEV * ADC_NUM_CH - 1] = (uint32_t)AUTO_RST << 16;

    // LPSPI を 32bit フレーム + TX/RX DMA リクエスト有効に設定 (endTransaction しない=保持)
    spi.beginTransaction(spiSettings);
    IMXRT_LPSPI4_S.TCR = (IMXRT_LPSPI4_S.TCR & 0xFFFFF000) | LPSPI_TCR_FRAMESZ(31);
    IMXRT_LPSPI4_S.FCR = LPSPI_FCR_RXWATER(0) | LPSPI_FCR_TXWATER(0);
    IMXRT_LPSPI4_S.DER = LPSPI_DER_TDDE | LPSPI_DER_RDDE;

    rxDma_.disable();
    rxDma_.source(*(volatile uint32_t*)&IMXRT_LPSPI4_S.RDR);
    rxDma_.destinationBuffer((uint32_t*)rxBuf_, sizeof(rxBuf_));
    rxDma_.triggerAtHardwareEvent(DMAMUX_SOURCE_LPSPI4_RX);
    rxDma_.disableOnCompletion();

    txDma_.disable();
    txDma_.sourceBuffer((uint32_t*)txCmds_, sizeof(txCmds_));
    txDma_.destination(*(volatile uint32_t*)&IMXRT_LPSPI4_S.TDR);
    txDma_.triggerAtHardwareEvent(DMAMUX_SOURCE_LPSPI4_TX);
    txDma_.disableOnCompletion();
}

template <size_t NUM_DEV>
void _adc<NUM_DEV>::startDma() {
    freqMeter_.tick();
    // 前回転送は disableOnCompletion で停止済み。count を戻して再 enable。
    rxDma_.destinationBuffer((uint32_t*)rxBuf_, sizeof(rxBuf_));
    txDma_.sourceBuffer((uint32_t*)txCmds_, sizeof(txCmds_));
    rxDma_.enable();  // RX を先に arm
    txDma_.enable();  // TX enable で LPSPI が TDR 要求 → 転送開始
    dmaCount_++;
}

template <size_t NUM_DEV>
void _adc<NUM_DEV>::latchDma() {
    for (size_t i = 0; i < NUM_DEV * ADC_NUM_CH; ++i)
        value[i] = rxBuf_[i] & 0xFFFF;
}
#endif  // ADC_DMA

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
// _adc<2> は現在未使用 (using Adc = _adc<1>)。2 台構成に戻す際は再度実体化する。
// ADC_DMA は単一デバイス専用のため _adc<2> を実体化すると static_assert で落ちる。
