#pragma once

#include <ArduinoJson.h>
#include "constants.hpp"

#define CONFIG_FILE_NAME "/config.json"
#define CONFIG_JSON_SIZE 1536

struct SensorValues {
    uint16_t apps1Min, apps1Max, apps2Min, apps2Max, ittrMin, ittrMax, tps1Min, tps1Max, tps2Min, tps2Max;
    uint16_t clutchMin, clutchMax;
    double idling;
    double normalMax;
    double restrictedMax;
};

struct PlausibilityCheckFlags {
    bool apps, tps, apps1, apps2, tps1, tps2, target, bps, bpsTps;
};

struct PidGains {
    double kP, kI, kD;
};

struct TargetCurve {
    double a4, a3, a2, a1;
};

enum class TransmissionType : uint8_t { IST, Normal };

struct GearPositionConfig {
    TransmissionType type;
    uint16_t istRawValues[GPS_IST_GEAR_COUNT];
    uint16_t normalRawValues[GPS_NORMAL_GEAR_COUNT];
};

struct ConfigModel {
    SensorValues sensorValues;
    PlausibilityCheckFlags plausibilityFlags;
    bool useIttr;
    PidGains pid;
    TargetCurve targetCurve;
    GearPositionConfig gps;

    void loadFromConstants();
    bool loadFromJson(const String& jsonStr);
    void toJson(JsonObject& out) const;
};
