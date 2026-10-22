#include "sensor_logger.hpp"

#include <TimeLib.h>

#include "debug_logger.hpp"
#include "util/stop_watch.hpp"

bool SensorLogger::begin() {
    active_ = false;
    const StopWatch watch(clock_);
    if (!store_.begin())
        return false;

    // 絶対時刻の基準: RTC と、それを読んだ瞬間の t_us の組 (ディレクトリ内の全ファイルで同じ)。SD の初期化の後に読む
    rtcEpoch_ = (year() >= 2020) ? (uint32_t)now() : 0;
    rtcTUs_ = clock_.nowUs64();

    if (!openNextFile())
        return false;
    lastSyncMs_ = clock_.nowMs();
    ring_.clear();
    active_ = true;
    DebugLogger::log("SD log: started in %lu ms (dir %u)", (unsigned long)(watch.elapsedUs() / 1000),
                     (unsigned)store_.dirNumber());
    return true;
}

bool SensorLogger::openNextFile() {
    const StopWatch watch(clock_);
    if (!store_.openNextFile(FILE_BYTES))
        return false;
    const LogHeader h = {
        .magic = {'K', 'A', 'R', 'T', 'L', 'O', 'G', '1'},  // SENSOR_LOG_MAGIC (NUL 終端なしの 8 文字)
        .version = SENSOR_LOG_VERSION,
        .record_size = sizeof(LogRecord),
        .log_hz = SENSOR_LOG_HZ,
        .dir_number = store_.dirNumber(),
        .file_index = store_.fileIndex(),
        .reserved = 0,
        .rtc_epoch = rtcEpoch_,
        .rtc_t_us = rtcTUs_,
    };
    static_assert(sizeof(SENSOR_LOG_MAGIC) - 1 == sizeof(h.magic), "magic は 8 文字");
    if (!store_.write(&h, sizeof(h)))
        return false;
    fileRemaining_ = RECORDS_PER_FILE;
    stats_.noteSdUs(watch.elapsedUs());
    DebugLogger::log("SD log: file %u (%lu ms)", (unsigned)store_.fileIndex(), (unsigned long)(watch.elapsedUs() / 1000));
    return true;
}

void SensorLogger::service(uint32_t nowMs) {
    if (!active_)
        return;
    if (ring_.pending() >= WRITE_MIN_RECORDS) {
        if (fileRemaining_ == 0 && !openNextFile()) {
            stop("open");
            return;
        }
        // 連続している区間を、最大 8 KB とファイルの残りで区切って 1 回で書く (レコードはファイルをまたがない)
        const LogRing::Span span = ring_.peek();
        uint32_t n = span.count;
        if (n > WRITE_MAX_RECORDS)
            n = WRITE_MAX_RECORDS;
        if (n > fileRemaining_)
            n = fileRemaining_;
        const StopWatch watch(clock_);
        const bool ok = store_.write(span.data, (size_t)n * sizeof(LogRecord));
        stats_.noteSdUs(watch.elapsedUs());  // SD の書き込みが走った回の所要を捕まえる
        if (!ok) {
            stop("write");
            return;
        }
        ring_.pop(n);
        fileRemaining_ -= n;
    }
    // 定期 sync: ディレクトリ (有効な長さ) + データを媒体へ反映 (電源断で失う分を SYNC_INTERVAL 以内に)
    if ((uint32_t)(nowMs - lastSyncMs_) >= SYNC_INTERVAL_MS) {
        lastSyncMs_ = nowMs;
        const StopWatch watch(clock_);
        const bool ok = store_.sync();
        stats_.noteSdUs(watch.elapsedUs());
        if (!ok)
            stop("sync");
    }
}

void SensorLogger::stop(const char* why) {
    active_ = false;  // 以後 tick は数えるだけ
    store_.closeFile();
    ring_.clear();
    DebugLogger::log("SD log: stopped (%s failed)", why);
}

void SensorLogger::close() {
    if (!active_)
        return;
    active_ = false;
    // 残りを全部書く (ファイルの残りに収まる分だけ。切り替えが要るほどの量はリングに入らない)
    for (;;) {
        const LogRing::Span span = ring_.peek();
        uint32_t n = span.count;
        if (n == 0)
            break;
        if (n > fileRemaining_)
            n = fileRemaining_;
        if (n == 0 || !store_.write(span.data, (size_t)n * sizeof(LogRecord)))
            break;
        ring_.pop(n);
        fileRemaining_ -= n;
    }
    store_.closeFile();  // sync して閉じる
    ring_.clear();
    DebugLogger::log("SD log: closed");
}

bool SensorLogger::format() {
    active_ = false;
    store_.closeFile();
    ring_.clear();
    const StopWatch watch(clock_);
    const bool ok = store_.format();
    DebugLogger::log("SD format: %s (%lu ms)", ok ? "ok" : "failed", (unsigned long)(watch.elapsedUs() / 1000));
    return ok && begin();  // 新しいディレクトリで再開
}
