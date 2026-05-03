#ifndef _DEBUG_LOGGER_H_
#define _DEBUG_LOGGER_H_

#include <stdarg.h>
#include <stdint.h>

#include "log_writer.hpp"

#define DEBUG_LOGGER_MAX_WRITERS 4

class DebugLogger {
   public:
    static void addWriter(LogWriter* writer);
    static void log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

   private:
    static LogWriter* writers[DEBUG_LOGGER_MAX_WRITERS];
    static uint8_t writerCount;
};

#endif
