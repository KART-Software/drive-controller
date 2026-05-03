#include "serial_debug_writer.hpp"
#include "serial_protocol.hpp"

void SerialDebugWriter::writev(const char* fmt, va_list args) {
    SerialProtocol::sendDebugv(fmt, args);
}
