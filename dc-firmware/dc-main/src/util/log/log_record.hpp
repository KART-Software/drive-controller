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
#define SENSOR_LOG_VERSION 0        // 開発中は 0 のまま
#define SENSOR_LOG_HZ 1000          // 収集レート (tickISR の 1 ms)

// LogRecord.flags のビット。SensorHub が立てるもの / 呼び出し側が立てるものを混在。
#define LOG_FLAG_MANUAL (1u << 0)     // EtcTarget manual (hub)
#define LOG_FLAG_ITTR (1u << 1)       // ターゲット=ITTR 由来 (hub)
#define LOG_FLAG_SHUTDOWN_SIG (1u << 2)  // SHUTDOWN_SIG_IN = HIGH (hub)
#define LOG_FLAG_VALID (1u << 3)      // plausibility 現在 OK (caller)
#define LOG_FLAG_AUTOSHIFT (1u << 4)  // CAN auto-shift ON (caller)
#define LOG_FLAG_LAUNCH (1u << 5)     // CAN launch ON (caller)
#define LOG_FLAG_STARTER_RELAY (1u << 6)   // セルモーターリレー ON (caller)
#define LOG_FLAG_STARTER_SWITCH (1u << 7)  // セルスイッチ (0x740 byte3、途絶時は 0) (caller)

// 各ファイルの先頭 (32 B)
struct __attribute__((packed)) LogHeader {
    char magic[8];         // "KARTLOG1"
    uint16_t version;      // SENSOR_LOG_VERSION
    uint16_t record_size;  // sizeof(LogRecord) (デコーダの検査用)
    uint16_t log_hz;       // SENSOR_LOG_HZ
    uint16_t dir_number;   // 起動ごとの通し番号 (dc_NNNN)
    uint16_t file_index;   // ディレクトリ内の連番 (0000.bin = 0)
    uint16_t reserved;     // 0
    uint32_t rtc_epoch;    // 起動時の Unix 時刻 (0 = RTC 未設定)。ディレクトリ内の全ファイルで同じ
    uint64_t rtc_t_us;     // rtc_epoch を読んだ瞬間の t_us。絶対時刻 = rtc_epoch + (t_us - rtc_t_us) / 1e6
};

// 176 B 固定 (packed)。4 B の項目は 4 の倍数のオフセットに並べてある。
struct __attribute__((packed)) LogRecord {
    uint64_t t_us;            // tick の時刻 (Clock の µs、起動 = 0。割り込みが止まっても進む)  ※ caller
    uint16_t adc[8];          // ADC 生 ch0..7  (hub)
    float apps1, apps2, ittr; // 変換済みセンサ値 (hub)
    float tps1, tps2, bps;    //   〃
    float clutch;             // クラッチ位置 % (hub)
    float accel[3];           // mg (移動平均後)  (hub)
    float gyro[3];            // dps (移動平均後) (hub)
    float wheel_hz[4];        // FL,FR,RL,RR のパルス周波数 [Hz] (hub)
    float engine_hz;          // エンジンのパルス周波数 [Hz] (hub)
    float clutch_hz;          // クラッチ後 (出力軸) のパルス周波数 [Hz] (hub)
    uint32_t wheel_count[4];  // FL,FR,RL,RR の累計エッジ数 (hub)
    float target_tp;          // ターゲットスロットル (hub)
    float duty;               // モーター実印加 duty u [%]。停止 (コースト) 中は 0  ※ caller
    uint32_t errors;          // プラウシビリティ エラービットマスク  ※ caller
    uint16_t flags;           // LOG_FLAG_* (hub 分 + caller 分)
    int8_t gear;              // -1=不明,0=N,1-6 (hub)
    uint8_t mode;             // EtcTarget::Mode (hub)
    uint8_t autoshift_state;  // shift::AutoShifter::State  ※ caller
    // ── ETC 同定実験モード (etc::ExperimentRunner) ※ caller ──
    uint8_t exp_type;   // 0=none, 1=point_dwell, 2=release, 3=step
    uint8_t exp_index;  // シーケンス内インデックス (dwell 0-220 / release 0-110 / step 0-109)
    uint8_t exp_phase;  // 0=idle, 1=settling, 2=holding, 3=coasting
    // ── loop 停止の可視化 (LoopStats::live() = 直前の窓と現在の窓のここまでの大きい方。loop が集計した値を tick が読む) ※ caller ──
    uint32_t loop_max_us;  // loop 1 周の最大 [µs] (停止を記録するため uint32)
    uint32_t sd_max_us;    // SD の write / sync 1 回の最大 [µs]
    uint16_t tick_max_us;  // tickISR 1 回の最大 [µs]
    uint16_t log_drops;    // 記録されなかったレコードの累計 (無効な間 + リングの溢れ。65535 で飽和)
    // ── CAN バスの状態 (CanBus::state()。毎 ms のレジスタ読み) ※ caller ──
    uint8_t can_fault;  // 0 = error active, 1 = error passive, 2 = bus-off (CanState::Fault)
    uint8_t can_tec;    // 送信エラーカウンタ (TEC)
    uint8_t can_rec;    // 受信エラーカウンタ (REC)
    uint8_t reserved;   // 0 (4 B 揃え)
    // ── 回転の換算値 (センサーが CAN 0x607 / 0x609 向けに出しているもの。Hz は生の計測値として上に残す) (hub) ──
    float wheel_kmh[4];  // FL,FR,RL,RR の車速 [km/h] (タイヤ径が未設定なら 0)
    float engine_rpm;    // エンジン回転数 [rpm]
    float clutch_rpm;    // クラッチ後 (出力軸) の回転数 [rpm]
};

// レイアウトを固定する (tools/decode_log.py の HEADER_FMT / RECORD_FMT と一致させる)
static_assert(sizeof(LogHeader) == 32, "LogHeader は 32 B (decode_log.py HEADER_FMT)");
static_assert(sizeof(LogRecord) == 176, "LogRecord は 176 B (decode_log.py RECORD_FMT)");
static_assert(offsetof(LogRecord, wheel_count) == 100 && offsetof(LogRecord, loop_max_us) == 136 &&
                  offsetof(LogRecord, can_fault) == 148 && offsetof(LogRecord, wheel_kmh) == 152,
              "4 B の項目は 4 の倍数のオフセットに置く");
