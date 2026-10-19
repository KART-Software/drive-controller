#pragma once

#include <Arduino.h>
#include <FreqMeasureMulti.h>

#include "interfaces/sensor/edge_source.hpp"

// FlexPWM の入力キャプチャ (FreqMeasureMulti) で測った周期の列からエッジの時刻を組み立てる (docs/loop_nonblocking_spec.md §6.7)。
// 車輪速のピン (24 / 25 / 28 / 36) 用。各ピンは別々の FlexPWM サブモジュールであること (同じサブモジュールの 2 ピンは同時に測れない)。
//
// - 片側: 立ち上がりどうしの周期 (RAISING)。両エッジ: 立ち上がり→立ち下がり、立ち下がり→立ち上がりの半周期を交互に (ALTERNATE)。
// - 周期は IP バスクロックで測った正確な値なので、最初のエッジの位置だけ tick の時刻で決め (誤差 1 ms 以内)、あとは周期を足していく。
//   FlexPWM と PIT (Clock) は同じ水晶から作られているのでずれない。止まっていた間の周期もライブラリが一周を数えて測る (約 28 s まで)。
// - FreqMeasureMulti はエッジごとに割り込み (優先度 48) でバッファ (24 個) に積む。tick が毎 ms 取り出す。
class FreqMeasureEdgeSource final : public EdgeSource {
   public:
    explicit FreqMeasureEdgeSource(uint8_t pin) : pin_(pin) {}

    bool begin(bool bothEdges) override;
    void setBothEdges(bool bothEdges) override;
    EdgeSnapshot poll(uint64_t nowUs) override;

   private:
    uint8_t pin_;
    FreqMeasureMulti fm_;
    bool enabled_ = false;
    uint32_t count_ = 0;
    bool anchored_ = false;  // lastEdgeUs_ が有効
    bool skipFirst_ = true;  // begin 直後の 1 個目は前回値が無く周期が不正
    double lastEdgeUs_ = 0.0;
    bool lostPending_ = false;
};
