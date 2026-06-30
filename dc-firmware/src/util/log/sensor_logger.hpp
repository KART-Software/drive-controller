#pragma once

#include <Arduino.h>

#include "binary_writer.hpp"
#include "log_record.hpp"

// センサーデータのバイナリロガー (facade)。テキスト用 DebugLogger に対応するバイナリ版。
// フォーマット (LogHeader/LogRecord, log_record.hpp) のヘッダ→レコードを BinaryWriter へ書く。
// レコードの中身は buildLogRecord (log_record_builder) が組み立てる。

class SensorLogger {
   public:
    explicit SensorLogger(BinaryWriter& writer) : writer_(writer) {}
    bool begin();                    // writer.begin() + ヘッダ書込
    void log(const LogRecord& rec);  // 1 レコード追記
    void service(uint32_t nowMs);    // 定期 flush (loop から毎回呼ぶ)
    bool active() const { return writer_.active(); }

   private:
    BinaryWriter& writer_;
    uint32_t lastSyncMs_ = 0;
    static constexpr uint32_t SYNC_INTERVAL_MS = 1000;
};
