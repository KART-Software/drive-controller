#pragma once

#include <stddef.h>
#include <stdint.h>

// SD バイナリログのフォーマット定義 (ヘッダ + レコード)。docs/loop_nonblocking_spec.md §7.2 が一次ソース。
// 生成側 (buildLogRecord, log_record_builder) と書き出し側 (SensorLogger) で共有する純データ。
// 高レート向けにバイナリ固定長。スケーリングは後処理 (tools/decode_log.py)。
// レイアウトを変えたら tools/decode_log.py を合わせること。開発中は version を 0 のまま上げない (固まったら上げる)。
//
// ファイル構成: [LogHeader] [LogRecord][LogRecord]... (レコードはファイルをまたがない)

#define SENSOR_LOG_MAGIC "KARTLOG1"  // 8 文字ちょうど
#define SENSOR_LOG_VERSION 0
#define SENSOR_LOG_HZ 1000

// LogRecord.flags
#define LOG_FLAG_MANUAL (1u << 0)  // EtcTarget manual
#define LOG_FLAG_ITTR (1u << 1)    // ターゲットが ITTR 由来
#define LOG_FLAG_SHUTDOWN_SIG (1u << 2)  // SIG_IN = HIGH
#define LOG_FLAG_VALID (1u << 3)   // plausibility が現在 OK
#define LOG_FLAG_AUTOSHIFT (1u << 4)
#define LOG_FLAG_LAUNCH (1u << 5)
#define LOG_FLAG_STARTER_RELAY (1u << 6)
#define LOG_FLAG_STARTER_SWITCH (1u << 7)  // 途絶時は 0

// 各ファイルの先頭 (32 B)
struct __attribute__((packed)) LogHeader {
    char magic[8];
    uint16_t version;
    uint16_t record_size;  // デコーダの検査用
    uint16_t log_hz;
    uint16_t dir_number;   // 起動ごとの通し番号 (dc_NNNN)
    uint16_t file_index;   // ディレクトリ内の連番 (0000.bin = 0)
    uint16_t reserved;
    uint32_t rtc_epoch;    // 起動時の Unix 時刻 (0 = RTC 未設定)。ディレクトリ内の全ファイルで同じ
    uint64_t rtc_t_us;     // rtc_epoch を読んだ瞬間の t_us。絶対時刻 = rtc_epoch + (t_us - rtc_t_us) / 1e6
};

// 176 B 固定 (packed)。4 B の項目は 4 の倍数のオフセットに並べてある。
struct __attribute__((packed)) LogRecord {
    uint64_t t_us;            // tick の時刻 (Clock)。割り込みが止まっても進む
    uint16_t adc[8];
    float apps1, apps2, ittr; // 変換済み [%]
    float tps1, tps2, bps;    // [%] [%] [psi]
    float clutch;             // [%]
    float accel[3];           // [mg] 移動平均後
    float gyro[3];            // [dps] 移動平均後
    float wheel_hz[4];        // FL, FR, RL, RR のパルス周波数
    float engine_hz;
    float clutch_hz;          // クラッチ後 (出力軸)
    uint32_t wheel_count[4];  // FL, FR, RL, RR の累計エッジ数
    float target_tp;
    float duty;               // 実印加 [%]。停止 (コースト) 中は 0
    uint32_t errors;          // プラウシビリティのエラービット
    uint16_t flags;           // LOG_FLAG_*
    int8_t gear;              // -1 = 不明, 0 = N, 1〜6
    uint8_t mode;             // EtcTarget::Mode
    uint8_t autoshift_state;  // shift::AutoShifter::State
    uint8_t exp_type;   // etc::ExperimentRunner::Type
    uint8_t exp_index;  // シーケンス内の位置 (dwell 0〜220 / release 0〜110 / step 0〜109)
    uint8_t exp_phase;  // etc::ExperimentRunner::Phase
    // loop 停止の可視化 (LoopStats::live()。loop が集計した値を tick が読む)
    uint32_t loop_max_us;  // 停止を記録するため uint32
    uint32_t sd_max_us;    // write / sync 1 回の最大
    uint16_t tick_max_us;
    uint16_t log_drops;    // 記録されなかったレコードの累計 (無効な間 + リングの溢れ。65535 で飽和)
    uint8_t can_fault;  // CanState::Fault (0 active / 1 passive / 2 bus-off)
    uint8_t can_tec;
    uint8_t can_rec;
    uint8_t reserved;   // 4 B 揃え
    // 回転の換算値 (センサーが出す。Hz は生の計測値として上に残す)
    float wheel_kmh[4];  // FL, FR, RL, RR。タイヤ径が未設定なら 0
    float engine_rpm;
    float clutch_rpm;
};

// レイアウトを固定する (tools/decode_log.py の HEADER_FMT / RECORD_FMT と一致させる)
static_assert(sizeof(LogHeader) == 32, "LogHeader は 32 B (decode_log.py HEADER_FMT)");
static_assert(sizeof(LogRecord) == 176, "LogRecord は 176 B (decode_log.py RECORD_FMT)");
static_assert(offsetof(LogRecord, wheel_count) == 100 && offsetof(LogRecord, loop_max_us) == 136 &&
                  offsetof(LogRecord, can_fault) == 148 && offsetof(LogRecord, wheel_kmh) == 152,
              "4 B の項目は 4 の倍数のオフセットに置く");
