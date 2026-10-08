#pragma once

#include <stdint.h>

#include "can/can_controller.hpp"
#include "etc/experiment_runner.hpp"
#include "etc/motor_controller.hpp"
#include "etc/plausibility_validator.hpp"
#include "sensor/sensor_hub.hpp"
#include "shift/auto_shifter.hpp"
#include "util/loop_stats.hpp"
#include "util/log/log_record.hpp"

// 各サブシステムの現在値から SD ログ 1 レコードを組み立てる純粋関数。
// 全入力を const 参照で受け取り、const アクセサのみ使うため副作用なし
// (plausibility の valid は PlausibilityValidator が直近評価をキャッシュした値を読む)。
// buildLogRecord が読む相手 (すべて const 参照、副作用なし)
struct LogSources {
    const SensorHub& hub;
    const etc::PlausibilityValidator& plausibility;
    const CanController& can;
    const shift::AutoShifter& shifter;
    const etc::MotorController& motor;
    const etc::ExperimentRunner& experiment;
};

LogRecord buildLogRecord(uint32_t t_ms, const LogSources& src, const LoopStats::Snapshot& sys);
