#ifndef _SERIAL_DEBUG_WRITER_H_
#define _SERIAL_DEBUG_WRITER_H_

#include "util/log/log_writer.hpp"

class SerialDebugWriter : public LogWriter {
   public:
    void writev(const char* fmt, va_list args) override;
};

#endif
