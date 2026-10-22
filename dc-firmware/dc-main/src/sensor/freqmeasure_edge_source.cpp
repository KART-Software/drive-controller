#include "freqmeasure_edge_source.hpp"

bool FreqMeasureEdgeSource::begin(bool bothEdges) {
    enabled_ = fm_.begin(pin_, bothEdges ? FREQMEASUREMULTI_ALTERNATE : FREQMEASUREMULTI_RAISING);
    anchored_ = false;
    skipFirst_ = true;
    lostPending_ = true;
    return enabled_;
}

void FreqMeasureEdgeSource::setBothEdges(bool bothEdges) {
    if (!enabled_)
        return;
    fm_.end();
    begin(bothEdges);
}

EdgeSnapshot FreqMeasureEdgeSource::poll(uint64_t nowUs) {
    EdgeSnapshot s;
    if (!enabled_)
        return s;
    const double usPerCount = 1e6 / (double)F_BUS_ACTUAL;
    while (fm_.available()) {
        const uint32_t period = fm_.read();  // 前のエッジからこのエッジまで (IP バスクロックのカウント)
        count_++;
        if (skipFirst_ || !anchored_) {
            skipFirst_ = false;
            anchored_ = true;
            lastEdgeUs_ = (double)nowUs;  // 位置だけ tick の時刻で決める
            continue;
        }
        lastEdgeUs_ += (double)period * usPerCount;
        s.hasEdge = true;
    }
    if (anchored_ && lastEdgeUs_ > (double)nowUs)
        lastEdgeUs_ = (double)nowUs;  // 位置の誤差でまだ来ていない時刻にならないように
    s.count = count_;
    if (s.hasEdge) {
        s.edgeIndex = count_;
        s.edgeUs = lastEdgeUs_;
    }
    if (lostPending_) {
        s.lost = true;
        lostPending_ = false;
    }
    return s;
}
