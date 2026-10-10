#pragma once

#include "command_router.hpp"
#include "configurator.hpp"
#include "control/control_input.hpp"
#include "etc/experiment_runner.hpp"
#include "etc/motor_controller.hpp"
#include "sensor/sensors.hpp"

struct CommandContainer {
    Configurator& configurator;
    etc::MotorController& motorController;
    EtcTarget& target;
    etc::ExperimentRunner& experimentRunner;
    ControlInput& controlInput;
};

class CommandController {
   public:
    CommandController(Configurator& configurator,
                      etc::MotorController& motorController,
                      EtcTarget& target,
                      etc::ExperimentRunner& experimentRunner,
                      ControlInput& controlInput);
    void registerCommands(CommandRouter& router);

   private:
    CommandContainer container;
};

