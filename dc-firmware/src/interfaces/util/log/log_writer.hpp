#pragma once

#include <stdarg.h>
#include <stdint.h>

// debug メッセージの出力先 (DebugLogger が全 writer に配る)。
class LogWriter {
   public:
    virtual ~LogWriter() = default;
    // timestampUs = DebugLogger::log が呼ばれた時刻 (起動からの µs)。
    // どの文脈 (ISR・割り込み禁止中を含む) から呼ばれてもよいように実装すること。
    // その場で出せない出力先は、自分でためて service() で出す
    virtual void writev(uint64_t timestampUs, const char* fmt, va_list args) = 0;
    // loop から毎周 (DebugLogger::service 経由)。ためた分を出す (ためない出力先は何もしない)
    virtual void service() {}
};
