#pragma once

#include <Print.h>

#include "debug_logger.hpp"

// Print を受け取る相手 (Teensy コアの CrashReport など) の出力を、行ごとに DebugLogger::log() に流す。loop / setup から
class DebugPrint : public Print {
   public:
    size_t write(uint8_t c) override {
        if (c == '\n') {
            line_[n_] = 0;
            if (n_ > 0)
                DebugLogger::log("%s", line_);
            n_ = 0;
        } else if (c != '\r' && n_ < sizeof(line_) - 1) {
            line_[n_++] = (char)c;
        }
        return 1;
    }
    void flush() override {
        if (n_ > 0)
            write('\n');
    }

   private:
    char line_[120];
    size_t n_ = 0;
};
