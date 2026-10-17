#include "sensor_logger.hpp"

#include <TimeLib.h>

#include "util/stop_watch.hpp"

bool SensorLogger::begin() {
    if (!writer_.begin())
        return false;

    // rtc_epoch と boot_ms は同じ瞬間に取る (デコーダが rtc_epoch + (t_ms - boot_ms) で絶対時刻を出す)。
    // SD の初期化 (writer_.begin) の後に読む
    const uint32_t nowMs = clock_.nowMs();

    LogHeader h = {
        .magic = {'K', 'A', 'R', 'T', 'L', 'O', 'G', '1'},  // SENSOR_LOG_MAGIC (NUL 終端なしの 8 文字)
        .version = SENSOR_LOG_VERSION,
        .record_size = sizeof(LogRecord),
        .rtc_epoch = (year() >= 2020) ? (uint32_t)now() : 0,
        .log_hz = SENSOR_LOG_HZ,
        .boot_ms = nowMs,
    };
    static_assert(sizeof(SENSOR_LOG_MAGIC) - 1 == sizeof(h.magic), "magic は 8 文字");
    writer_.write(&h, sizeof(h));

    lastSyncMs_ = nowMs;
    return true;
}

void SensorLogger::log(const LogRecord& rec) {
    if (writer_.active()) {
        const StopWatch watch(clock_);
        writer_.write(&rec, sizeof(rec));
        stats_.noteSdUs(watch.elapsedUs());  // SD セクタ書き込みが走った回の所要を捕まえる
    }
}

void SensorLogger::service(uint32_t nowMs) {
    // 定期 flush: ディレクトリ+データを媒体へ反映 (電源断時のロストを SYNC_INTERVAL 以内に)
    if (writer_.active() && (uint32_t)(nowMs - lastSyncMs_) >= SYNC_INTERVAL_MS) {
        lastSyncMs_ = nowMs;
        const StopWatch watch(clock_);
        writer_.flush();
        stats_.noteSdUs(watch.elapsedUs());
    }
}
