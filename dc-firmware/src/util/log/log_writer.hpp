#pragma once

#include <stdarg.h>

class LogWriter {
   public:
    virtual ~LogWriter() = default;
    virtual void writev(const char* fmt, va_list args) = 0;
};

