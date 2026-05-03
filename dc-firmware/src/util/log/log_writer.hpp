#ifndef _LOG_WRITER_H_
#define _LOG_WRITER_H_

#include <stdarg.h>

class LogWriter {
   public:
    virtual ~LogWriter() = default;
    virtual void writev(const char* fmt, va_list args) = 0;
};

#endif
