#pragma once

#include <Arduino.h>

#include "interfaces/sensor/edge_source.hpp"

// QuadTimer でエッジを数え、同じモジュールの空きチャネルでエッジ時刻をキャプチャする (docs/loop_nonblocking_spec.md §6.7)。
// 対応ピン: 14 (QuadTimer3 ch2、キャプチャ ch0)、15 (ch3、キャプチャ ch1)。割り込みは使わず、tick が毎 ms 読む。
//
// - 数えるチャネル: 入力ピンのエッジを数える (片側 = 立ち上がり、両エッジ = 立ち上がりと立ち下がり)。
// - キャプチャチャネル: IP バスクロック ÷64 で自走し、副入力 (SCS) に入力ピンを選んで CAPT にエッジ時刻を取る。
//   CAPT は IEF を消すまで最初の 1 回しか更新されない (RM §54.4.4) ので、poll のたびに「エッジ数を読む → IEF を消す →
//   エッジ数を読む」を同じ値になるまで繰り返し、次の CAPT を「その値 + 1 番目のエッジ」とする。
// - 16 bit のキャプチャ側の時間軸は poll ごとに上位を伸ばす (一周約 28 ms)。poll の間隔が一周に近づいたら (flash 書き込みで
//   tick が止まった等) 対応を見失ったとして lost を返す。
// - ピン 18 / 19 (ch1 / ch0) はキャプチャに使うので analogWrite できなくなる。
class QuadTimerEdgeSource final : public EdgeSource {
   public:
    explicit QuadTimerEdgeSource(uint8_t pin) : pin_(pin) {}

    bool begin(bool bothEdges) override;
    void setBothEdges(bool bothEdges) override;
    EdgeSnapshot poll(uint64_t nowUs) override;

   private:
    uint32_t readCount();  // 数えるチャネルの 16 bit を累計に伸ばす
    void configure(bool bothEdges);
    double tickUs() const { return 64.0 * 1e6 / (double)F_BUS_ACTUAL; }

    uint8_t pin_;
    IMXRT_TMR_t* timer_ = nullptr;
    uint8_t countCh_ = 0;
    uint8_t capCh_ = 0;
    bool enabled_ = false;

    uint16_t lastCountRaw_ = 0;
    uint32_t count_ = 0;

    bool anchored_ = false;    // キャプチャ側の時間軸と Clock の対応が取れている
    uint16_t lastCapRaw_ = 0;
    uint64_t cap64_ = 0;       // キャプチャ側の時間軸 (伸ばしたもの) の、直近 poll 時点の値
    double offsetUs_ = 0.0;    // Clock の µs = cap64 * tickUs + offsetUs_
    uint64_t lastPollUs_ = 0;
    bool armed_ = false;       // IEF を消した後のエッジ番号 (countAtClear_) が有効
    uint32_t countAtClear_ = 0;
    bool lostPending_ = false;  // 設定の変更などで次の poll で lost を返す
};
