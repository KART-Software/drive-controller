#pragma once

#include "util/log/log_writer.hpp"

class SerialDebugWriter : public LogWriter {
   public:
    void writev(const char* fmt, va_list args) override;
};

