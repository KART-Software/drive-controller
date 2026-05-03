#pragma once

#include <Arduino.h>
#include "error_handler.hpp"
#include "sensor/sensors.hpp"

#define SENSOR_IMPLAUSIBLE_THRESHOLD_TIME 100
#define APPS_TPS_TARGET_IMPLAUSIBLE_THRESHOLD_TIME 1000
#define BPS_TPS_IMPLAUSIBLE_THRESHOLD_TIME 1000
#define SENSOR_SAME_POSITION_THRESHOLD 10.0
#define PLAUSIBLE_DURATION 500

namespace etc {

class PlausibilityValidator {
   public:
    PlausibilityValidator(const Apps& apps1,
                          const Apps& apps2,
                          const Ittr& ittr,
                          const Tps& tps1,
                          const Tps& tps2,
                          const EtcTarget& target,
                          const Bps& bps);
    void initialize();
    bool isCurrentlyValid();
    bool isValid();
    void setCheckFlags(bool apps,
                       bool tps,
                       bool apps1,
                       bool apps2,
                       bool tps1,
                       bool tps2,
                       bool target,
                       bool bps,
                       bool bpsTps);
    ErrorHandler& getErrorHandler() { return errorHandler; }
    bool appsCheckFlag = false, tpsCheckFlag = false, apps1CheckFlag = false, apps2CheckFlag = false,
         tps1CheckFlag = false, tps2CheckFlag = false, targetCheckFlag = false, bpsCheckFlag = false,
         bpsTpsCheckFlag = false;

   private:
    ErrorHandler errorHandler = ErrorHandler();
    const Apps &apps1, &apps2;
    const Ittr& ittr;
    const Tps &tps1, &tps2;
    const EtcTarget& target;
    const Bps& bps;
    bool isValidAllTime;
    unsigned long lastTpsPlausibleTime, lastAppsPlausibleTime, lastTps1CircuitValidTime, lastTps2CircuitValidTime,
        lastApps1CircuitValidTime, lastApps2CircuitValidTime, lastAppsTpsTargetValidTime, lastBpsCircuitValidTime,
        lastBpsTpsPlausibleTime;

    void initParameters();
    bool isAppsPlausible();
    bool isTpsPlausible();
    bool isApps1CircuitValid();
    bool isApps2CircuitValid();
    bool isTps1CircuitValid();
    bool isTps2CircuitValid();
    bool isAppsTpsTargetValid();
    bool isBpsCircuitValid();
    bool isBpsTpsPlausible();
};

}  // namespace etc

