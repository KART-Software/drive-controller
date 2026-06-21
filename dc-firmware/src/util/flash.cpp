#include "flash.hpp"
#include <pb_decode.h>
#include <pb_encode.h>
#include "util/log/debug_logger.hpp"

bool Flash::initialize() {
    for (int i = 0; i < BEGIN_FS_LIMIT_TIMES; i++) {
        if (fs.begin(256 * 1024)) {
            // 容量警告のみ出す。設定が消える自動フォーマットは行わない。
            // 容量が逼迫した場合はホストから明示的に消去すること。
            if (fs.totalSize() > 0 && fs.usedSize() > fs.totalSize() * 9 / 10) {
                DebugLogger::log("LittleFS nearly full: %u/%u bytes",
                                 (unsigned)fs.usedSize(), (unsigned)fs.totalSize());
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

bool Flash::format() {
    // LittleFS を消去。保持したいデータ (config 等) は呼び出し側が事前に RAM 保持し、
    // フォーマット後に書き戻すこと。
    bool ok = fs.quickFormat();
    if (!ok)
        DebugLogger::log("Flash format failed");
    return ok;
}
