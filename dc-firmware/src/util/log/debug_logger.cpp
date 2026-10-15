#include "debug_logger.hpp"

const Clock* DebugLogger::clock_ = nullptr;
LogWriter* DebugLogger::writers[DEBUG_LOGGER_MAX_WRITERS] = {};
uint8_t DebugLogger::writerCount = 0;

void DebugLogger::initialize(const Clock& clock) {
    clock_ = &clock;
}

void DebugLogger::addWriter(LogWriter* writer) {
    if (writerCount < DEBUG_LOGGER_MAX_WRITERS) {
        writers[writerCount++] = writer;
    }
}

void DebugLogger::log(const char* fmt, ...) {
    const uint64_t t = clock_ != nullptr ? clock_->nowUs64() : 0;
    va_list args;
    va_start(args, fmt);
    for (uint8_t i = 0; i < writerCount; i++) {
        va_list copy;
        va_copy(copy, args);
        writers[i]->writev(t, fmt, copy);
        va_end(copy);
    }
    va_end(args);
}

void DebugLogger::service() {
    for (uint8_t i = 0; i < writerCount; i++) {
        writers[i]->service();
    }
}
