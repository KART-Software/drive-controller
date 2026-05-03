#pragma once

#include "command_router.hpp"
#include "configurator.hpp"
#include "etc/motor_controller.hpp"
#include "sensor/sensors.hpp"

struct CommandContainer {
    Configurator& configurator;
    etc::MotorController& motorController;
    EtcTarget& target;
};

class CommandController {
   public:
    CommandController(Configurator& configurator, etc::MotorController& motorController, EtcTarget& target);
    void registerCommands(CommandRouter& router);

   private:
    CommandContainer container;
};

