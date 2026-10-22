#pragma once

#include <stdarg.h>
#include <stdint.h>

#include "interfaces/util/clock.hpp"
#include "interfaces/util/log/log_writer.hpp"

#define DEBUG_LOGGER_MAX_WRITERS 4

// debug メッセージの唯一の入口。どの文脈 (loop・ISR・割り込み禁止中) からでも log() を呼んでよい。
//
// - log() は呼ばれた時刻 (µs、initialize() で渡した Clock の nowUs64()) を付けて、全 LogWriter に渡すだけ。
//   initialize() より前のメッセージは時刻 0。
// - その場で出せない文脈かどうか (ISR の中など) は writer が判断し、出せなければ自分でためて service() で出す
//   (例: SerialDebugWriter)。そのため出る順は呼ばれた順と前後しうる。並べ替えは読む側が時刻で行う。
// - ISR から呼ぶときの書式は整数と文字列だけにする (writer が ISR の中で書式化することがあり、newlib の
//   浮動小数点の書式化は内部で malloc する)。小数が要る値は 10 倍して整数で出す。
// - addWriter() は setup() の中だけで呼ぶ (log() は writer の一覧を排他なしで読む)。
class DebugLogger {
   public:
    // setup() の最初に呼ぶ。clock は以後ずっと生きていること
    static void initialize(const Clock& clock);
    static void addWriter(LogWriter* writer);
    static void log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
    // loop から毎周。各 writer がためたメッセージを出す
    static void service();

   private:
    static const Clock* clock_;
    static LogWriter* writers[DEBUG_LOGGER_MAX_WRITERS];
    static uint8_t writerCount;
};
