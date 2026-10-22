#pragma once

#include <Arduino.h>

// 固定サイズ N の移動平均。add() はサンプリング ISR (または loop) の 1 か所から、getAvg() はどの文脈からでも呼ぶ。
// - 合計は持ち越さず、getAvg() のたびに N 個を足し直す。合計を「古い値を引いて新しい値を足す」で持ち越すと、
//   float の丸め誤差が加減算のたびに積み重なって平均が少しずつずれるため。ADC の生の値 (0〜65535) は N = 60 個足しても
//   float が整数を正確に表せる範囲 (2^24) に収まり、丸めは起きない。
// - add() が書き換えるのは values[] の 1 個だけ (32 bit の 1 命令)。getAvg() の途中に add() が割り込んでも
//   (または add() の途中に優先度の高い tickISR が読んでも)、どの要素も本物のサンプルなので「1 サンプル新しい組」に
//   なるだけで、壊れた値は見えない。割り込みを止める必要はない。
// - getAvg() は N に比例する。
template <uint N>
class MovingAverage {
   public:
    void add(float value) {
        values[index] = value;
        index = (index + 1 < N) ? index + 1 : 0;
    }

    float getAvg() const {
        // 足し算を 4 列に分ける。1 列だと前の足し算の結果を待つ (FPU のレイテンシ) ので、並べて待ちを重ねる。
        // 実測 (Teensy 4.1): N = 60 で 1 列 205 → 4 列 141 サイクル、N = 8 は 4 列が最速。5 列以上はほぼ縮まない (読み出しで頭打ち)
        float s[4] = {};
        uint i = 0;
        for (; i + 4 <= N; i += 4) {
            s[0] += values[i];
            s[1] += values[i + 1];
            s[2] += values[i + 2];
            s[3] += values[i + 3];
        }
        for (; i < N; i++) {
            s[0] += values[i];
        }
        return ((s[0] + s[1]) + (s[2] + s[3])) / static_cast<float>(N);
    }

   private:
    volatile float values[N] = {};
    uint index = 0;
};
