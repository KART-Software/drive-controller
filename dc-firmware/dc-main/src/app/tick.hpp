#pragma once

#include <stdint.h>

#include "can/can_controller.hpp"
#include "control/control_input.hpp"
#include "etc/experiment_runner.hpp"
#include "etc/motor_controller.hpp"
#include "etc/plausibility_validator.hpp"
#include "interfaces/util/clock.hpp"
#include "log_record_builder.hpp"
#include "sensor/sensor_hub.hpp"
#include "shift/auto_shifter.hpp"
#include "starter/starter_controller.hpp"
#include "util/flash_write_gate.hpp"
#include "util/log/sensor_logger.hpp"
#include "util/loop_stats.hpp"

// tick (1 ms) のロジックが使う部品 (docs/loop_nonblocking_spec.md §3.3)。main.cpp の Parts が持ち、isr/tick_isr が受け取って渡す
struct TickDeps {
    const Clock& clock;
    SensorHub& hub;
    ControlInput& control;
    etc::PlausibilityValidator& plausibility;
    etc::MotorController& motor;
    etc::ExperimentRunner& experiment;
    shift::AutoShifter& shifter;
    starter::StarterController& starter;
    CanController& can;
    SensorLogger& logger;
    LoopStats& stats;
    FlashWriteGate& flashGate;
    const LogSources& logSources;
};

namespace app {

// 1 ms ごと (GPT2、優先度 64)。時間の決まった処理はすべてここ (§6.2)。n = 何回目の tick (CAN 送信の周期に使う)。
// 状態は持たない。ISR から Serial / SD / flash / delay を呼ばないこと (debug は DebugLogger::log でよい。
// USB への出力は SerialDebugWriter がキューにためて loop で送る)
void tick(const TickDeps& d, uint32_t n);

}  // namespace app
