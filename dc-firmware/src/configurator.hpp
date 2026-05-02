#ifndef _CONFIGURATOR_H_
#define _CONFIGURATOR_H_

#include "config_model.hpp"
#include "flash.hpp"
#include "motor_controller.hpp"
#include "plausibility_validator.hpp"
#include "proto/drive_controller.pb.h"
#include "sensor/sensor_hub.hpp"

class Configurator {
   public:
    Configurator(SensorHub& hub, MotorController& motorController, PlausibilityValidator& plausibilityValidator);
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
    bool importConfig(const dc_Config& cfg);
    void save();
    void revert();
    Flash flash;
    ConfigModel config;
    Apps &apps1, &apps2;
    Tps &tps1, &tps2;
    Ittr& ittr;
    Target& target;
    MotorController& motorController;
    PlausibilityValidator& plausibilityValidator;
    bool configChanged = false;
    void loadConfigFromFlash();
    void calibrate();
};

#endif
