#pragma once

#include <stdint.h>

// 回転センサーのハードウェアから tick ごとに取るもの (docs/loop_nonblocking_spec.md §6.7)。
// 前回の poll 以降に来たエッジのうち、どれか 1 つの (番号, 時刻) を返す。どのエッジでもよい (QuadTimer は最初の、
// FreqMeasure は最後のエッジ) が、番号と時刻は必ず同じエッジのものであること (計算はこの組の差から回転を出す)。
struct EdgeSnapshot {
    uint32_t count = 0;      // 累計のエッジ数
    bool hasEdge = false;    // edgeIndex / edgeUs が前回の poll 以降に来たエッジを指している
    uint32_t edgeIndex = 0;  // そのエッジの番号 (count と同じ数え方。1 番目のエッジ = 1)
    double edgeUs = 0.0;     // そのエッジの時刻 [µs] (Clock の時間軸)
    bool lost = false;       // 時刻の対応を見失った (タイマーの一周を追えなかった等)。履歴を捨てること
};

// 回転センサーのハードウェア (エッジを数え、エッジの時刻を取る)。tickISR からだけ使う。
class EdgeSource {
   public:
    virtual ~EdgeSource() = default;

    virtual bool begin(bool bothEdges) = 0;      // false = このピンでは使えない
    virtual void setBothEdges(bool bothEdges) = 0;
    // nowUs = tick の時刻 (Clock の µs)。前回の poll 以降の状態を返す
    virtual EdgeSnapshot poll(uint64_t nowUs) = 0;
};
