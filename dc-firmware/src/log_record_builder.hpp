#pragma once

#include <stdint.h>

#include "control/control_input.hpp"
#include "etc/experiment_runner.hpp"
#include "etc/motor_controller.hpp"
#include "etc/plausibility_validator.hpp"
#include "sensor/sensor_hub.hpp"
#include "shift/auto_shifter.hpp"
#include "starter/starter_controller.hpp"
#include "util/loop_stats.hpp"
#include "util/log/log_record.hpp"

// 各サブシステムの現在値から SD ログ 1 レコードを組み立てる純粋関数。tickISR の最後 (docs/loop_nonblocking_spec.md §6.2 の 11)
// から毎 ms 呼ばれ、同じ tick の判断結果がそのレコードに入る。
// 全入力を const 参照で受け取り、const アクセサのみ使うため副作用なし
// (plausibility の valid は PlausibilityValidator が直近評価をキャッシュした値を読む)。
// buildLogRecord が読む相手 (すべて const 参照、副作用なし)
struct LogSources {
    const SensorHub& hub;
    const etc::PlausibilityValidator& plausibility;
    const ControlInput& control;
    const shift::AutoShifter& shifter;
    const etc::MotorController& motor;
    const etc::ExperimentRunner& experiment;
    const starter::StarterController& starter;
};

// tUs: この tick の時刻 (Clock の µs)、nowMs: 同じ時刻の ms (ControlInput の途絶判定に渡す)、
// sys: loop が集計した所要時間 (LoopStats::live()。loop が書く値を tick が読むが、診断用なので途中の値でもよい)
LogRecord buildLogRecord(uint64_t tUs, uint32_t nowMs, const LogSources& src, const LoopStats::Snapshot& sys);
