#pragma once

#include "etc/motor_controller.hpp"
#include "etc/plausibility_validator.hpp"
#include "launch/launch_controller.hpp"
#include "proto/drive_controller.pb.h"
#include "sensor/sensor_hub.hpp"
#include "util/flash.hpp"

#define CONFIG_FILE_NAME "/config.pb"

class Configurator {
   public:
    Configurator(SensorHub& hub,
                 etc::MotorController& motorController,
                 etc::PlausibilityValidator& plausibilityValidator,
                 launch::LaunchController& launchController);
    void initialize();
    void calibrateFromFlash();

    void setAppsMin();
    void setAppsMax();
    void setTpsMin();
    void setTpsMax();
    void setIdling();
    void setTargetBound(float idling, float normalMax, float restrictedMax);
    void setPlausibilityFlags(const dc_EtcPlausibilityCheckFlags& flags);
    void setIttrFlag(bool val);
    void setPid(float kP, float kI, float kD);
    void setTargetCurve(const dc_EtcTargetCurve& curve);
    void setGpsGear(int8_t gear);
    bool importConfig(const dc_Config& cfg);
    void save();
    void revert();

    Flash flash;
    dc_Config config = dc_Config_init_zero;
    Apps &apps1, &apps2;
    Tps &tps1, &tps2;
    Ittr& ittr;
    EtcTarget& target;
    GearPositionSensor& gps;
    ClutchSensor& clutch;
    etc::MotorController& motorController;
    etc::PlausibilityValidator& plausibilityValidator;
    launch::LaunchController& launchController;
    bool configChanged = false;

   private:
    void loadDefault();
    void loadConfigFromFlash();
    void calibrate();
};
