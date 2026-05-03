#include "flash.hpp"
#include "util/log/debug_logger.hpp"

bool Flash::initialize() {
    for (int i = 0; i < BEGIN_FS_LIMIT_TIMES; i++) {
        if (fs.begin(256 * 1024)) {
            // 使用率が90%超の場合はフォーマット
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

void Flash::write(const char* fileName, const String& jsonStr) {
    if (fs.exists(fileName)) {
        fs.remove(fileName);
    }
    File file = fs.open(fileName, FILE_WRITE);
    if (!file) {
        DebugLogger::log("Flash write: open failed");
        return;
    }
    size_t written = file.write((const uint8_t*)jsonStr.c_str(), jsonStr.length());
    file.close();
    DebugLogger::log("Saved to %s (%u bytes)", fileName, (unsigned)written);
}

String Flash::read(const char* fileName) {
    File file = fs.open(fileName);
    if (!file || file.isDirectory()) {
        DebugLogger::log("Flash read: %s not found", fileName);
        return String();
    }
    String jsonStr = file.readString();
    file.close();
    DebugLogger::log("Loaded from %s (%u bytes)", fileName, (unsigned)jsonStr.length());
    return jsonStr;
}

void Flash::remove(const char* fileName) {
    fs.remove(fileName);
}