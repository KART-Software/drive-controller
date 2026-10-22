#include "edge_rotation_sensor.hpp"

#include <Arduino.h>

namespace {

inline void barrier() {
    __asm__ volatile("dmb" ::: "memory");
}

}  // namespace

bool EdgeRotationSensor::begin() {
    // setup で config を読んだ後に呼ぶ。置かれた設定をここで取り込んでからハードウェアを動かす (tick を待たない)
    if (hasPending_) {
        estimator_.setParams(pending_);
        hasPending_ = false;
    }
    enabled_ = source_.begin(estimator_.params().bothEdges);
    return enabled_;
}

void EdgeRotationSensor::setParams(const RotationParams& p) {
    // loop から。tick が pending_ をコピーする途中で書き換えないよう短く割り込みを止める
    noInterrupts();
    pending_ = p;
    hasPending_ = true;
    interrupts();
}

void EdgeRotationSensor::update(uint64_t nowUs) {
    if (hasPending_) {
        const RotationParams p = pending_;
        hasPending_ = false;
        const bool edgesChanged = p.bothEdges != estimator_.params().bothEdges;
        estimator_.setParams(p);
        if (edgesChanged && enabled_)
            source_.setBothEdges(p.bothEdges);
    }
    if (!enabled_)
        return;

    const EdgeSnapshot snap = source_.poll(nowUs);
    const RotationSample s = estimator_.update(snap, nowUs);

    seq_ = seq_ + 1;
    barrier();
    buf_ = s;
    count_ = snap.count;
    barrier();
    seq_ = seq_ + 1;
}

RotationSample EdgeRotationSensor::sample() const {
    for (;;) {
        const uint32_t before = seq_;
        barrier();
        const RotationSample s = buf_;
        barrier();
        if ((before & 1) == 0 && seq_ == before)
            return s;
    }
}
