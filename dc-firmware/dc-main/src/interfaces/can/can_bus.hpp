#pragma once

#include <stddef.h>
#include <stdint.h>

#include <array>

#include "can_frame.hpp"

// CAN バスの読み書き口 (docs/can_bus_spec.md §3)。使う側 (CanController) はハードウェアもライブラリも知らない。
// Zephyr の drivers/can.h の最小サブセット (can_frame / can_send / can_add_rx_filter / can_get_state) を手本にしている。

struct CanConfig {
    uint32_t bitrate;      // アービトレーション相 (classic ではこれだけ) [bit/s]
    bool fd = false;       // CAN FD を使う (今の実装は非対応: begin() が false を返す)
    uint32_t dataBitrate = 0;  // FD のデータ相 [bit/s] (fd のときだけ)
};

struct CanState {
    enum class Fault : uint8_t { Active, Passive, BusOff };
    Fault fault;       // エラー状態 (ESR1 FLTCONF)
    uint8_t txErrors;  // 送信エラーカウンタ (ECR TXERRCNT)
    uint8_t rxErrors;  // 受信エラーカウンタ (ECR RXERRCNT)
    bool synced;       // バスに同期している (11 bit の recessive を見た)
};

struct CanStats {
    uint32_t rxFrames;    // コールバックに渡したフレーム数
    uint32_t rxOverrun;   // 受信メールボックスを読む前に次のフレームが来て、前のを失った回数
    uint32_t txDropped;   // 空きメールボックスがなく捨てた数
    uint32_t isrCount;    // 割り込みの回数
};

// 受け入れる標準 ID。それ以外はハードウェアで落とす。ID ごとに受信メールボックスを 1 本使う (kMax 本まで)
struct CanRxFilters {
    static constexpr size_t kMax = 8;
    std::array<uint32_t, kMax> ids = {};
    size_t count = 0;
};

class CanBus {
   public:
    virtual ~CanBus() = default;
    // onRx は**受信割り込みの文脈**で、1 フレームごとに呼ばれる (短く済ませる。Serial / SD / malloc は禁止)
    using RxCallback = void (*)(const CanFrame& frame);

    // false = この設定には対応していない (FD、表にないビットレート)
    virtual bool begin(const CanConfig& config, const CanRxFilters& filters, RxCallback onRx, uint8_t irqPriority) = 0;
    // 空きの送信メールボックスに書いて true。空きがなければ捨てて false (周期送信なので次で送る。キューは持たない)。
    // 設定で対応していないフレーム (classic で fd / len > 8) も捨てて false
    virtual bool send(const CanFrame& frame) = 0;
    virtual CanState state() const = 0;
    virtual CanStats stats() const = 0;
};
