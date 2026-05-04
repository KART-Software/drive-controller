#pragma once

#include "config_model.hpp"
#include "etc/motor_controller.hpp"
#include "etc/plausibility_validator.hpp"
#include "proto/drive_controller.pb.h"
#include "sensor/sensor_hub.hpp"
#include "util/flash.hpp"

class Configurator {
   public:
    Configurator(SensorHub& hub,
                 etc::MotorController& motorController,
                 etc::PlausibilityValidator& plausibilityValidator);
    void initialize();
    void calibrateFromFlash();
    void getConfigJson(JsonObject& out);
    // Fill a proto Config message reflecting the current configuration.
    void getConfigProto(dc_Config& out);

    void setAppsMin();
    void setAppsMax();
    void setTpsMin();
    void setTpsMax();
    void setIdling();
    void setTargetBound(double idling, double normalMax, double restrictedMax);
    void setPlausibilityFlags(const PlausibilityCheckFlags& flags);
    void setIttrFlag(bool val);
    void setPid(double kP, double kI, double kD);
    void setTargetCurve(const TargetCurve& curve);
    void setGpsGear(int8_t gear);
    bool importConfig(const dc_Config& cfg);
    void save();
    void revert();
    Flash flash;
    ConfigModel config;
    Apps &apps1, &apps2;
    Tps &tps1, &tps2;
    Ittr& ittr;
    EtcTarget& target;
    GearPositionSensor& gps;
    ClutchSensor& clutch;
    etc::MotorController& motorController;
    etc::PlausibilityValidator& plausibilityValidator;
    bool configChanged = false;
    void loadConfigFromFlash();
    void calibrate();
};
