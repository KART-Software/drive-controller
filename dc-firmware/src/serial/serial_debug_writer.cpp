#include "serial_debug_writer.hpp"

#include <Arduino.h>
#include <stdio.h>

#include "serial_protocol.hpp"

namespace {

uint32_t readIpsr() {
    uint32_t v;
    __asm__ volatile("mrs %0, ipsr" : "=r"(v));
    return v;
}

uint32_t readPrimask() {
    uint32_t v;
    __asm__ volatile("mrs %0, primask" : "=r"(v));
    return v;
}

// ISR の中 (IPSR != 0) か、割り込み禁止中 (PRIMASK != 0) ならその場で送れない。
// USB がつながっていない間も送らずにためる (Serial を調べるのは ISR の外にいるときだけ)
bool mustDefer() {
    return readIpsr() != 0 || readPrimask() != 0 || !Serial;
}

}  // namespace

void SerialDebugWriter::writev(uint64_t timestampUs, const char* fmt, va_list args) {
    if (mustDefer()) {
        enqueue(timestampUs, fmt, args);
    } else {
        service();  // ためてあった分を先に送る (ほぼ呼ばれた順になる。厳密な順は時刻で読む側が並べ替える)
        SerialProtocol::sendDebugv(timestampUs, fmt, args);
    }
}

void SerialDebugWriter::service() {
    if (mustDefer())
        return;
    while (tail_ != head_) {
        const Slot& s = slots_[tail_ % kSlots];
        send(s.timestampUs, "%s", s.text);
        tail_ = tail_ + 1;
    }
    // 件数と時刻は ISR が割り込み禁止の中で組で書くので、こちらも組で読む (64 bit の時刻が途中で
    // 書き換わったり、件数と時刻が別の回のものになったりしないように)
    const uint32_t primask = readPrimask();
    __disable_irq();
    const uint32_t d = dropped_;
    const uint64_t lastUs = lastDroppedUs_;
    if (primask == 0)
        __enable_irq();
    if (d != reportedDropped_) {
        send(lastUs, "debug: %lu messages dropped (queue full)", (unsigned long)(d - reportedDropped_));
        reportedDropped_ = d;
    }
}

void SerialDebugWriter::enqueue(uint64_t timestampUs, const char* fmt, va_list args) {
    const uint32_t primask = readPrimask();
    __disable_irq();
    const uint32_t h = head_;
    if (h - tail_ >= kSlots) {
        dropped_ = dropped_ + 1;
        lastDroppedUs_ = timestampUs;
    } else {
        Slot& s = slots_[h % kSlots];
        s.timestampUs = timestampUs;
        vsnprintf(s.text, kLen, fmt, args);
        head_ = h + 1;  // 文字列を書き終えてから公開する
    }
    if (primask == 0)
        __enable_irq();
}

void SerialDebugWriter::send(uint64_t timestampUs, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    SerialProtocol::sendDebugv(timestampUs, fmt, args);
    va_end(args);
}
