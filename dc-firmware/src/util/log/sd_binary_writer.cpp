#include "sd_binary_writer.hpp"

#include <SD.h>
#include <TimeLib.h>
#include <stdio.h>  // snprintf

#include "serial/serial_protocol.hpp"

// 単一インスタンス前提でファイルハンドルを file-static に (ヘッダへ SD.h を波及させない)。
static File sdFile;

void SdBinaryWriter::makeFilename(char* out, size_t n) {
    if (year() >= 2020) {
        // RTC 設定済み: 日時ファイル名 dc_log_YYYYMMDD_HHMMSS.bin
        snprintf(out, n, "dc_log_%04d%02d%02d_%02d%02d%02d.bin", year(), month(), day(), hour(),
                 minute(), second());
    } else {
        // RTC 未設定: 空き番号 dc_log_NNNN.bin
        for (uint16_t i = 1; i < 10000; i++) {
            snprintf(out, n, "dc_log_%04u.bin", i);
            if (!SD.exists(out))
                return;
        }
        snprintf(out, n, "dc_log_9999.bin");
    }
}

bool SdBinaryWriter::begin() {
    active_ = false;
    if (!SD.begin(BUILTIN_SDCARD)) {
        SerialProtocol::sendDebugf("SD: no card, logging disabled");
        return false;
    }
    char name[32];  // "dc_log_YYYYMMDD_HHMMSS.bin" + NUL
    makeFilename(name, sizeof(name));
    sdFile = SD.open(name, FILE_WRITE);
    if (!sdFile) {
        SerialProtocol::sendDebugf("SD: open failed (%s)", name);
        return false;
    }
    active_ = true;
    SerialProtocol::sendDebugf("SD log -> %s", name);
    return true;
}

void SdBinaryWriter::write(const void* data, size_t len) {
    if (active_)
        sdFile.write((const uint8_t*)data, len);  // SD ライブラリが 512B 単位にバッファ
}

void SdBinaryWriter::flush() {
    if (active_)
        sdFile.flush();  // ディレクトリ+データを SD へ反映 (電源断対策)
}
