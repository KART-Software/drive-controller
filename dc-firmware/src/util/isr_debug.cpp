#include "isr_debug.hpp"

#include <stdarg.h>
#include <stdio.h>

namespace {

constexpr uint32_t kSlots = 16;  // 2 のべき乗
constexpr size_t kLen = 96;

char slots[kSlots][kLen];
volatile uint32_t head = 0;  // 書き手 (ISR) だけが進める
volatile uint32_t tail = 0;  // 読み手 (loop) だけが進める
volatile uint32_t dropped = 0;

}  // namespace

void isrDebugf(const char* fmt, ...) {
    const uint32_t h = head;
    if (h - tail >= kSlots) {
        dropped = dropped + 1;
        return;
    }
    va_list args;
    va_start(args, fmt);
    vsnprintf(slots[h % kSlots], kLen, fmt, args);
    va_end(args);
    head = h + 1;  // 文字列を書き終えてから公開する
}

void drainIsrDebug(void (*fn)(const char* msg)) {
    while (tail != head) {
        fn(slots[tail % kSlots]);
        tail = tail + 1;
    }
}

uint32_t isrDebugDropped() {
    return dropped;
}
