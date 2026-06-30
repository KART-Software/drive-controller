#include "sensor_logger.hpp"

#include <TimeLib.h>

bool SensorLogger::begin() {
    if (!writer_.begin())
        return false;

    LogHeader h = {};
    memcpy(h.magic, SENSOR_LOG_MAGIC, 8);
    h.version = SENSOR_LOG_VERSION;
    h.record_size = sizeof(LogRecord);
    h.rtc_epoch = (year() >= 2020) ? (uint32_t)now() : 0;
    h.log_hz = SENSOR_LOG_HZ;
    h.boot_ms = millis();
    writer_.write(&h, sizeof(h));

    lastSyncMs_ = millis();
    return true;
}

void SensorLogger::log(const LogRecord& rec) {
    if (writer_.active())
        writer_.write(&rec, sizeof(rec));
}

void SensorLogger::service(uint32_t nowMs) {
    // 定期 flush: ディレクトリ+データを媒体へ反映 (電源断時のロストを SYNC_INTERVAL 以内に)
    if (writer_.active() && (uint32_t)(nowMs - lastSyncMs_) >= SYNC_INTERVAL_MS) {
        lastSyncMs_ = nowMs;
        writer_.flush();
    }
}
