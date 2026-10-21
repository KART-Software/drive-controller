#pragma once

#include <stdint.h>

#include "interfaces/sensor/edge_source.hpp"
#include "interfaces/sensor/rotation_sensor.hpp"
#include "sensor/edge_rate_estimator.hpp"

// RotationSensor の実装: EdgeSource (ハードウェア) + EdgeRateEstimator (計算) (docs/loop_nonblocking_spec.md §6.7)。
//
// - update() は tickISR から毎 ms。sample() / count() はどの文脈から読んでもよい。
//   tick が書き loop が読むので、番号付きのバッファ (seqlock) で組を揃える: 書く間は番号を奇数にし、
//   読む側は番号が偶数で前後が同じになるまで読み直す (割り込みは止めない)。
// - setParams() は config の適用 (Configurator) から。設定を置き、setup 中なら begin() が、実行中なら次の update() (tick) が
//   取り込む (履歴を捨てる)。tick が回っている間に計算の途中で設定が変わらないよう、直接は書き換えない。
class EdgeRotationSensor final : public RotationSensor {
   public:
    explicit EdgeRotationSensor(EdgeSource& source) : source_(source) {}

    bool begin();  // setup から (tick 開始前)。config の適用 (setParams) の後に呼ぶと、その設定で動き出す
    void setParams(const RotationParams& p);
    void update(uint64_t nowUs);

    RotationSample sample() const override;
    uint32_t count() const override { return count_; }
    uint32_t pulses() const override { return estimator_.params().bothEdges ? count_ / 2 : count_; }

   private:
    EdgeSource& source_;
    EdgeRateEstimator estimator_;
    bool enabled_ = false;

    RotationParams pending_;
    volatile bool hasPending_ = false;

    volatile uint32_t seq_ = 0;  // 奇数 = 書き込み中
    RotationSample buf_;
    volatile uint32_t count_ = 0;
};
