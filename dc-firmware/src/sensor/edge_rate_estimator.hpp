#pragma once

#include <stddef.h>
#include <stdint.h>

#include "interfaces/sensor/edge_source.hpp"
#include "interfaces/sensor/rotation_sensor.hpp"

// 回転センサー 1 つ分の設定 (config RotationSensorConfig を tick 用に直したもの)
struct RotationParams {
    uint32_t teeth = 1;           // 1 回転あたりの歯数
    uint32_t windowTeeth = 1;     // 窓の最大の歯数 N
    uint32_t maxWindowUs = 20000; // 窓の最大時間 T
    uint32_t staleUs = 300000;    // この時間エッジが来なければ 0
    bool bothEdges = true;        // 立ち上がりと立ち下がりの両方を数える
};

// (エッジ番号, 時刻) の履歴から回転を出す (docs/loop_nonblocking_spec.md §6.7)。ハードウェアに依存しない。
// tickISR から毎 ms update() する。
//
// - 窓は「N 歯か最大時間 T の短いほう」。両端は同じ向きのエッジに揃える (両エッジ時はエッジ間隔を偶数にして
//   デューティ比のずれを打ち消す)。1 歯の間隔が T より長い低回転では直近 1 歯分。1 歯分そろう前は半歯の値を仮に出す。
// - エッジが来ない間は、直前の 1 歯の間隔より長くなった時点から「1 歯 ÷ 経過時間」を上限として値を下げる。
//   staleUs で 0 にして履歴を捨てる。
// - 時刻の対応を見失ったとき (EdgeSnapshot::lost) と設定を変えたときは、履歴だけ捨てて直前の値は保つ
//   (新しい履歴で測れるまでの間、使う側が 0 を見ないように。止まったかどうかは staleUs で判定する)。
class EdgeRateEstimator {
   public:
    void setParams(const RotationParams& p);  // 履歴を捨てる (値は保ち、rpm は新しい歯数で換算し直す)
    const RotationParams& params() const { return p_; }
    void reset();  // 値も含めてすべて捨てる (止まった)

    RotationSample update(const EdgeSnapshot& s, uint64_t nowUs);

   private:
    struct Point {
        uint32_t index;
        double us;
    };
    static constexpr size_t kHistory = 64;  // 2 のべき乗。1 tick に高々 1 点なので T (≤ 64 ms) 分は残る

    const Point& at(size_t ago) const { return hist_[(head_ - 1 - ago) & (kHistory - 1)]; }
    void clearHistory();
    bool measure(RotationSample& out);

    RotationParams p_;
    Point hist_[kHistory] = {};
    size_t head_ = 0;  // 次に書く位置
    size_t size_ = 0;
    RotationSample measured_;    // 最後にエッジから測った値
    double toothUs_ = 0.0;       // measured_ の 1 歯の時間
    uint32_t lastCount_ = 0;
    uint64_t lastCountChangeUs_ = 0;  // エッジ数が最後に増えた tick の時刻
    bool counting_ = false;           // lastCount_ が有効
};
