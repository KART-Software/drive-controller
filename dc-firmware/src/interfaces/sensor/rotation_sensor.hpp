#pragma once

#include <stdint.h>

// 回転センサー 1 回分の計測結果 (docs/loop_nonblocking_spec.md §6.7)。tickISR が毎 ms 更新する。
struct RotationSample {
    float hz = 0.0f;        // 歯 (パルス) の周波数 [Hz]。0 = 停止
    float rpm = 0.0f;       // 回転数 [rpm] (= hz / 歯数 * 60)
    // この値が代表する時刻 (窓の中心、Clock の時間軸)。精度はハードウェアによる: QuadTimer (エンジン・クラッチ後) は
    // µs 未満、FreqMeasure (車輪) は最初のエッジの位置合わせで最大 1 ms ずれる (間隔は正確なので hz / rpm には影響しない)
    uint64_t timeUs = 0;
    uint32_t windowUs = 0;  // 窓の長さ [µs] (0 = 値なし)
    uint16_t edges = 0;     // 窓に入ったエッジ間隔の数 (0 = 値なし)
};

// 回転センサーの読み取り口 (エンジン・クラッチ後・車輪)。中身のハードウェア (QuadTimer / FlexPWM) は見せない。
// loop・tickISR・それより優先度の低い ISR から読んでよく、組の揃った値を返す。
// **tickISR (優先度 64) より優先度の高い割り込みから sample() / hz() / rpm() を呼ばないこと**: 値は tick が書き、
// 読む側は書き込み中なら読み直す (seqlock) ので、tick が書いている途中に割り込んで読むと、tick が書き終われず読み直しが終わらない。
class RotationSensor {
   public:
    virtual ~RotationSensor() = default;

    virtual RotationSample sample() const = 0;
    virtual uint32_t count() const = 0;  // 累計のエッジ数 (両エッジなら立ち上がり + 立ち下がり)

    float hz() const { return sample().hz; }
    float rpm() const { return sample().rpm; }
};
