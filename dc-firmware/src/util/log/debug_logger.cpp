#include "debug_logger.hpp"

LogWriter* DebugLogger::writers[DEBUG_LOGGER_MAX_WRITERS] = {};
uint8_t DebugLogger::writerCount = 0;

void DebugLogger::addWriter(LogWriter* writer) {
    if (writerCount < DEBUG_LOGGER_MAX_WRITERS) {
        writers[writerCount++] = writer;
    }
}

void DebugLogger::log(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    if (writerCount == 1) {
        writers[0]->writev(fmt, args);
    } else {
        for (uint8_t i = 0; i < writerCount; i++) {
            va_list copy;
            va_copy(copy, args);
            writers[i]->writev(fmt, copy);
            va_end(copy);
        }
    }
    va_end(args);
}
