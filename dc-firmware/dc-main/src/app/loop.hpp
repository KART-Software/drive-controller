#pragma once

#include "can/can_controller.hpp"
#include "commands/command_router.hpp"
#include "control/control_input.hpp"
#include "etc/motor_controller.hpp"
#include "etc/plausibility_validator.hpp"
#include "interfaces/util/clock.hpp"
#include "launch/launch_controller.hpp"
#include "sensor/sensor_view.hpp"
#include "util/every.hpp"
#include "util/log/sensor_logger.hpp"
#include "util/loop_stats.hpp"

// loop() のロジックが使う部品 (docs/loop_nonblocking_spec.md §3.3)。main.cpp の Parts が持ち、loop() から直接渡す。
// Every は状態なので Parts が持つ (ロジックは状態を持たない)
struct LoopDeps {
    const Clock& clock;
    const SensorView& hub;
    etc::PlausibilityValidator& plausibility;  // getErrorHandler() が非 const
    const etc::MotorController& motor;
    const ControlInput& control;
    const CanController& can;
    launch::LaunchController& launch;
    SensorLogger& logger;
    LoopStats& stats;
    CommandRouter& router;
    Every& statsWindow;
    Every& launchTick;
    Every& telemetry;
};

namespace app {

// loop() から毎周。時間の決まった処理は tick にあり、ここに残すのは遅れてよいものだけ (§3.1)
void loop(const LoopDeps& d);

}  // namespace app
