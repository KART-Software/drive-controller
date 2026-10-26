#include "adc.hpp"

#include "util/stop_watch.hpp"

// LPSPI RX 待ちの最大スピン時間 (us)。setup の初期化 (writeRegister / transferCommand) で ADC が無応答でも
// 固まらないようにする。1 フレーム転送は ~3us (@10MHz) なので 10us で十分。
static constexpr uint32_t ADC_SPI_TIMEOUT_US = 10;

template <size_t NUM_DEV>
_adc<NUM_DEV>::_adc(const Clock& clock, uint8_t csPin, SPIClass& spi)
    : clock_(clock), spi(spi), csPin(csPin), freqMeter_(clock) {
    for (size_t i = 0; i < NUM_DEV * ADC_NUM_CH; ++i) {
        value[i] = 0;
    }
}

template <size_t NUM_DEV>
void _adc<NUM_DEV>::begin() {
    spi.begin();
    *(portConfigRegister(csPin)) = 3;

    for (uint8_t ch = 0; ch < ADC_NUM_CH; ++ch) {
        writeRegister(RANGE_SELECT_ADDR_0 + ch, RANGE_4);
    }
    writeRegister(CH_POWER_DOWN_ADDR, 0x00);  // 全ch有効
    writeRegister(AUTO_SEQ_EN_ADDR, 0xFF);    // 全ch自動シーケンス
    uint16_t dummy[NUM_DEV];
    transferCommand(AUTO_RST, dummy);
    beginDma();
}

// DMA 経路。CS は 32 bit フレームごと (CONT=0)
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
    const StopWatch watch(clock_);
    while (IMXRT_LPSPI4_S.RSR & LPSPI_RSR_RXEMPTY) {
        if (watch.elapsedUs() > ADC_SPI_TIMEOUT_US)
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
    const StopWatch watch(clock_);
    while (IMXRT_LPSPI4_S.RSR & LPSPI_RSR_RXEMPTY) {
        if (watch.elapsedUs() > ADC_SPI_TIMEOUT_US)
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

template class _adc<1>;
// _adc<2> は現在未使用 (using Adc = _adc<1>)。2 台構成に戻す際は再度実体化する。
// DMA の読み出しは単一デバイス専用のため _adc<2> を実体化すると static_assert で落ちる。
