#pragma once

#include <stdint.h>

#include "log_record.hpp"

// tick → loop のレコードのリング (docs/loop_nonblocking_spec.md §7.3)。書き手 (push) は tick だけ、
// 読み手 (peek / pop / clear) は loop だけ (SPSC)。
// - head_ (書き手が進める) と tail_ (読み手が進める) は 32 bit の単調増加で、M7 では 1 命令で読み書きされるので
//   割り込みを止めない。満杯なら新しいものを捨てる (順序を保つ)。
// - 本体 (2048 × 148 B ≈ 303 KB) は RAM2 に置くため、呼ぶ側が DMAMEM の配列を渡す。DMAMEM は初期化されないので
//   添字はこのクラスが持つ (DTCM で 0 初期化される)。
// - 読み手は「先頭から連続している区間」を値で受け取り (peek)、書き終えた分だけ進める (pop)。
class LogRing {
   public:
    static constexpr uint32_t kCapacity = 2048;  // 2 のべき乗。1 kHz で ≈ 2 s
    struct Span {
        const LogRecord* data;
        uint32_t count;
    };

    explicit LogRing(LogRecord (&buf)[kCapacity]) : buf_(buf) {}

    // tick: 満杯なら false (捨てた)
    bool push(const LogRecord& rec) {
        const uint32_t head = head_;
        if (head - tail_ >= kCapacity)
            return false;
        buf_[head & (kCapacity - 1)] = rec;
        asm volatile("" ::: "memory");  // 中身を書き終えてから head を進める
        head_ = head + 1;
        return true;
    }

    // loop
    uint32_t pending() const { return head_ - tail_; }
    Span peek() const {  // 先頭から連続している区間 (折り返しの手前まで)
        const uint32_t tail = tail_;
        const uint32_t n = head_ - tail;
        const uint32_t idx = tail & (kCapacity - 1);
        const uint32_t contiguous = kCapacity - idx;
        return {.data = &buf_[idx], .count = n < contiguous ? n : contiguous};
    }
    void pop(uint32_t count) {
        asm volatile("" ::: "memory");  // 中身を読み終えてから tail を進める
        tail_ = tail_ + count;
    }
    void clear() { tail_ = head_; }

   private:
    LogRecord (&buf_)[kCapacity];
    volatile uint32_t head_ = 0;  // 次に書く位置 (tick)
    volatile uint32_t tail_ = 0;  // 次に読む位置 (loop)
};
