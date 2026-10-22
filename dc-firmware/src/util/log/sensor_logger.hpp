#pragma once

#include <stdint.h>

#include "interfaces/util/clock.hpp"
#include "interfaces/util/log/log_store.hpp"
#include "log_record.hpp"
#include "log_ring.hpp"
#include "util/loop_stats.hpp"

// センサーデータのバイナリロガー (facade、docs/loop_nonblocking_spec.md §7)。テキスト用 DebugLogger に対応するバイナリ版。
// フォーマット (LogHeader / LogRecord、log_record.hpp) のヘッダをファイルごとに、レコードをリング経由で LogStore へ書く。
// レコードの中身は buildLogRecord (log_record_builder) が組み立てる。
// - tick (1 ms): capture() に「レコードの作り方」を渡す。有効なときだけ作ってリングへ積み、無効な間 (カードなし / 停止 /
//   フォーマット中) とリングが満杯のときは作らずに数えるだけ (log_drops)。作るかどうかは呼ぶ側に見せない。
// - loop: service() がリングに 4 KB 以上たまったら最大 8 KB をまとめて書き、100 MB ごとにファイルを切り替え、2 s ごとに sync する。
//   失敗したらその起動では止める (通知はしない。drops が増え続けるので telemetry の SysStats で分かる)。
// - format(): ログを止めて媒体をフォーマットし、新しいディレクトリで始め直す (コマンド用。loop から)。
class SensorLogger {
   public:
    // stats: write / sync 1 回ごとの所要を noteSdUs() で報告する先 (loop 停止の可視化)。clock: その所要と RTC の基準時刻
    SensorLogger(LogStore& store, LogRing& ring, LoopStats& stats, const Clock& clock)
        : store_(store), ring_(ring), stats_(stats), clock_(clock) {}

    bool begin();  // setup。false = 無効 (カードなし等。理由は store が debug に出す)
    bool active() const { return active_; }

    // tick から。build は LogRecord を返す関数 (buildLogRecord を呼ぶラムダ)。組み立てには約 3 µs かかるので、
    // 無効な間は呼ばない。テンプレートなので std::function (malloc) は使わない
    template <typename Build>
    void capture(Build&& build) {
        if (!active_ || !ring_.push(build()))
            drops_ = drops_ + 1;
    }

    // loop から
    void service(uint32_t nowMs);
    bool format();
    void close();  // 再起動の前に: リングに残っている分を書いて sync し、閉じる (以後 tick は数えるだけ)
    uint32_t drops() const { return drops_; }  // 記録されなかったレコードの累計

   private:
    bool openNextFile();  // 次のファイルを作ってヘッダを書く
    void stop(const char* why);

    LogStore& store_;
    LogRing& ring_;
    LoopStats& stats_;
    const Clock& clock_;
    volatile bool active_ = false;
    volatile uint32_t drops_ = 0;
    uint32_t rtcEpoch_ = 0;
    uint64_t rtcTUs_ = 0;
    uint32_t fileRemaining_ = 0;  // 今のファイルにあと何レコード書けるか
    uint32_t lastSyncMs_ = 0;

#ifndef SENSOR_LOG_FILE_BYTES
#define SENSOR_LOG_FILE_BYTES 100000000  // 1 ファイル 100 MB (≈ 11.3 分)。ベンチで切り替えを試すときはビルドフラグで小さくする
#endif
    static constexpr uint64_t FILE_BYTES = SENSOR_LOG_FILE_BYTES;
    static constexpr uint32_t RECORDS_PER_FILE = (uint32_t)((FILE_BYTES - sizeof(LogHeader)) / sizeof(LogRecord));
    static constexpr uint32_t WRITE_MIN_RECORDS = 4096 / sizeof(LogRecord);  // 4 KB たまったら書く
    static constexpr uint32_t WRITE_MAX_RECORDS = 8192 / sizeof(LogRecord);  // 1 回に最大 8 KB
    static constexpr uint32_t SYNC_INTERVAL_MS = 2000;
};
