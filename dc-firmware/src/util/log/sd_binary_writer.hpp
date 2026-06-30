#pragma once

#include <Arduino.h>

#include "binary_writer.hpp"

// Teensy 4.1 内蔵 SD スロット (SDIO) への BinaryWriter 実装。
// ファイル名は RTC 日時 (dc_log_YYYYMMDD_HHMMSS.bin)、RTC 未設定なら空き番号 dc_log_NNNN.bin。
// SD 未挿入なら begin()==false で以降 active()==false (全 no-op)。
// 注意: ファイルハンドルは .cpp の file-static で持つため単一インスタンス専用。
class SdBinaryWriter : public BinaryWriter {
   public:
    bool begin() override;
    void write(const void* data, size_t len) override;
    void flush() override;
    bool active() const override { return active_; }

   private:
    bool active_ = false;
    void makeFilename(char* out, size_t n);
};
