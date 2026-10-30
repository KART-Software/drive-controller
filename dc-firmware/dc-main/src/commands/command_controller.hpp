#pragma once

#include "command_router.hpp"
#include "configurator.hpp"
#include "control/control_input.hpp"
#include "etc/experiment_runner.hpp"
#include "etc/motor_controller.hpp"
#include "interfaces/util/clock.hpp"
#include "sensor/sensors.hpp"
#include "util/log/sensor_logger.hpp"

struct CommandContainer {
    Configurator& configurator;
    etc::MotorController& motorController;
    EtcTarget& target;
    etc::ExperimentRunner& experimentRunner;
    ControlInput& controlInput;
    SensorLogger& sensorLogger;
    const Clock& clock;  // 時刻で判定するコマンド (上書きの受け付け) が読む
};

class CommandController {
   public:
    CommandController(Configurator& configurator,
                      etc::MotorController& motorController,
                      EtcTarget& target,
                      etc::ExperimentRunner& experimentRunner,
                      ControlInput& controlInput,
                      SensorLogger& sensorLogger,
                      const Clock& clock);
    void registerCommands(CommandRouter& router);

   private:
    CommandContainer container;
};

