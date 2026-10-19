#include "edge_rate_estimator.hpp"

void EdgeRateEstimator::setParams(const RotationParams& p) {
    p_ = p;
    if (p_.teeth == 0)
        p_.teeth = 1;
    if (p_.windowTeeth == 0)
        p_.windowTeeth = 1;
    clearHistory();
    measured_.rpm = measured_.hz / (float)p_.teeth * 60.0f;  // hz (歯の周波数) は歯数に依らない
}

void EdgeRateEstimator::clearHistory() {
    head_ = 0;
    size_ = 0;
}

void EdgeRateEstimator::reset() {
    clearHistory();
    measured_ = RotationSample{};
    toothUs_ = 0.0;
    counting_ = false;
}

bool EdgeRateEstimator::measure(RotationSample& out) {
    if (size_ < 2)
        return false;
    const uint32_t perTooth = p_.bothEdges ? 2 : 1;
    const uint32_t maxEdges = p_.windowTeeth * perTooth;
    const Point& last = at(0);

    // 古い点ほど窓が広い。窓 (N 歯・T) に入る中で一番広い「同じ向きのエッジどうし」を選ぶ
    size_t inWindowEven = 0, inWindowAny = 0;  // 見つかった点 (何個前か、0 = なし)
    size_t beyondEven = 0, firstAny = 0;
    for (size_t ago = 1; ago < size_; ago++) {
        const Point& p = at(ago);
        const uint32_t d = last.index - p.index;
        const double dt = last.us - p.us;
        if (d == 0 || dt <= 0.0)
            continue;
        if (firstAny == 0)
            firstAny = ago;
        const bool even = (d % perTooth) == 0;
        if (d <= maxEdges && dt <= p_.maxWindowUs) {
            if (even && d >= perTooth)
                inWindowEven = ago;
            inWindowAny = ago;
        } else if (inWindowEven != 0) {
            break;  // 窓の中で決まっている
        } else if (even && d >= perTooth) {
            beyondEven = ago;  // 窓には入らないが一番近い 1 歯以上 (低回転で 1 歯が T より長いとき)
            break;
        }
    }

    size_t pick = inWindowEven;
    if (pick == 0 && inWindowAny != 0 && (last.index - at(inWindowAny).index) >= 4 * perTooth)
        pick = inWindowAny;  // 偶数がそろわない (tick ごとの点が飛び飛び) が、間隔が十分あればずれは小さい
    if (pick == 0)
        pick = beyondEven;  // 低回転: 直近 1 歯分 (T を超える)
    if (pick == 0)
        pick = firstAny;  // 1 歯分そろう前: 半歯の仮の値
    if (pick == 0)
        return false;

    const Point& p = at(pick);
    const uint32_t d = last.index - p.index;
    const double dt = last.us - p.us;
    const double teethInWindow = (double)d / perTooth;
    out.hz = (float)(teethInWindow * 1e6 / dt);
    out.rpm = out.hz / (float)p_.teeth * 60.0f;
    out.timeUs = (uint64_t)((last.us + p.us) * 0.5);
    out.windowUs = (uint32_t)dt;
    out.edges = (uint16_t)(d > 0xFFFF ? 0xFFFF : d);
    toothUs_ = dt / teethInWindow;
    return true;
}

RotationSample EdgeRateEstimator::update(const EdgeSnapshot& s, uint64_t nowUs) {
    if (s.lost)
        clearHistory();  // 値は保ち、新しい履歴で測れるまで待つ

    if (!counting_ || s.count != lastCount_) {
        lastCount_ = s.count;
        lastCountChangeUs_ = nowUs;
        counting_ = true;
    }

    if (s.hasEdge && (size_ == 0 || (int32_t)(s.edgeIndex - at(0).index) > 0)) {
        hist_[head_] = {s.edgeIndex, s.edgeUs};
        head_ = (head_ + 1) & (kHistory - 1);
        if (size_ < kHistory)
            size_++;
        RotationSample m;
        if (measure(m))
            measured_ = m;
    }

    // エッジが来ない間: 止まり判定と、上限による減衰
    const uint64_t quietUs = nowUs - lastCountChangeUs_;  // 本当のエッジ無し時間はこれ以上
    if ((size_ > 0 || measured_.hz > 0.0f) && quietUs >= p_.staleUs) {  // 履歴を捨てた後に止まった場合も 0 にする
        reset();
        counting_ = true;  // lastCount_ は保つ
        return RotationSample{};
    }
    RotationSample out = measured_;
    if (out.hz > 0.0f && quietUs > toothUs_ && quietUs > 0) {
        const float bound = (float)(1e6 / (double)quietUs);  // 次の歯まで少なくとも quietUs かかる
        if (bound < out.hz) {
            out.hz = bound;
            out.rpm = bound / (float)p_.teeth * 60.0f;
        }
    }
    return out;
}
