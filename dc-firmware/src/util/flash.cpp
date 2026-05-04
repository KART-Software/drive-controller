#include "flash.hpp"
#include <pb_decode.h>
#include <pb_encode.h>
#include "util/log/debug_logger.hpp"

bool Flash::initialize() {
    for (int i = 0; i < BEGIN_FS_LIMIT_TIMES; i++) {
        if (fs.begin(256 * 1024)) {
            if (fs.totalSize() > 0 && fs.usedSize() > fs.totalSize() * 9 / 10) {
                DebugLogger::log("LittleFS nearly full, formatting...");
                fs.quickFormat();
            }
            return true;
        }
    }
    DebugLogger::log("LittleFS begin failed.");
    return false;
}

bool Flash::writeProto(const char* fileName, const pb_msgdesc_t* fields, const void* msg) {
    uint8_t buf[kBufSize];
    pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
    if (!pb_encode(&stream, fields, msg)) {
        DebugLogger::log("Flash writeProto: encode failed");
        return false;
    }
    size_t len = stream.bytes_written;
    if (fs.exists(fileName))
        fs.remove(fileName);
    File file = fs.open(fileName, FILE_WRITE);
    if (!file) {
        DebugLogger::log("Flash writeProto: open failed");
        return false;
    }
    file.write(buf, len);
    file.close();
    DebugLogger::log("Saved %u bytes to %s", (unsigned)len, fileName);
    return true;
}

bool Flash::readProto(const char* fileName, const pb_msgdesc_t* fields, void* msg) {
    File file = fs.open(fileName);
    if (!file || file.isDirectory()) {
        DebugLogger::log("Flash readProto: %s not found", fileName);
        return false;
    }
    uint8_t buf[kBufSize];
    size_t len = file.read(buf, sizeof(buf));
    file.close();
    pb_istream_t stream = pb_istream_from_buffer(buf, len);
    if (!pb_decode(&stream, fields, msg)) {
        DebugLogger::log("Flash readProto: decode failed");
        return false;
    }
    DebugLogger::log("Loaded %u bytes from %s", (unsigned)len, fileName);
    return true;
}

void Flash::remove(const char* fileName) {
    fs.remove(fileName);
}
