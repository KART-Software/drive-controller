#pragma once

#include <Arduino.h>

// SD バイナリログのフォーマット定義 (ヘッダ + レコード)。
// 生成側 (buildLogRecord, log_record_builder) と書き出し側 (SensorLogger) で共有する純データ。
// 高レート向けにバイナリ固定長。スケーリングは後処理 (tools/decode_log.py)。
// レイアウト変更時は SENSOR_LOG_VERSION を上げ、tools/decode_log.py を合わせること。
//
// ファイル構成: [LogHeader] [LogRecord][LogRecord]...

#define SENSOR_LOG_MAGIC "KARTLOG1"  // 8 文字ちょうど
#define SENSOR_LOG_VERSION 2
#define SENSOR_LOG_HZ 1000  // 収集レート

// LogRecord.flags のビット。SensorHub が立てるもの / 呼び出し側が立てるものを混在。
#define LOG_FLAG_MANUAL (1u << 0)     // EtcTarget manual (hub)
#define LOG_FLAG_ITTR (1u << 1)       // ターゲット=ITTR 由来 (hub)
#define LOG_FLAG_SHUTDOWN_SIG (1u << 2)  // SHUTDOWN_SIG_IN = HIGH (hub)
#define LOG_FLAG_VALID (1u << 3)      // plausibility 現在 OK (caller)
#define LOG_FLAG_AUTOSHIFT (1u << 4)  // CAN auto-shift ON (caller)
#define LOG_FLAG_LAUNCH (1u << 5)     // CAN launch ON (caller)

struct __attribute__((packed)) LogHeader {
    char magic[8];         // "KARTLOG1"
    uint16_t version;      // SENSOR_LOG_VERSION
    uint16_t record_size;  // sizeof(LogRecord)
    uint32_t rtc_epoch;    // ログ開始時の Unix 時刻 (0=RTC未設定)
    uint32_t log_hz;       // SENSOR_LOG_HZ
    uint32_t boot_ms;      // 開始時 millis() (t_ms の基準)
};

// 128 バイト固定 (4 バイト整列)。u32/float は全て 4 整列オフセットに並べてある。
struct __attribute__((packed)) LogRecord {
    uint32_t t_ms;            // millis() (boot 基準)  ※ caller
    uint16_t adc[8];          // ADC 生 ch0..7  (hub)
    float apps1, apps2, ittr; // 変換済みセンサ値 (hub)
    float tps1, tps2, bps;    //   〃
    float accel[3];           // mg (平均後)  (hub)
    float gyro[3];            // dps (平均後) (hub)
    float wheel[4];           // FL,FR,RL,RR [Hz] (hub)
    float rpm;                // engine [Hz] (hub)
    float clutch_rpm;         // clutch 出力軸 [Hz] (hub)
    float target_tp;          // ターゲットスロットル (hub)
    float clutch;             // クラッチ位置 % (hub)
    uint32_t wheel_count[4];  // FL,FR,RL,RR パルス累積 (hub)
    uint32_t errors;          // プラウシビリティ エラービットマスク  ※ caller
    uint16_t flags;           // LOG_FLAG_* (hub 分 + caller 分)
    int8_t gear;              // -1=不明,0=N,1-6 (hub)
    uint8_t mode;             // EtcTarget::Mode (hub)
    uint8_t autoshift_state;  // shift::AutoShifter::State  ※ caller
    uint8_t _pad[3];          // 128B へ整列
};
