#pragma once

#include <Arduino.h>

#include "constants.hpp"
#include "util/moving_average.hpp"

#define MANUAL_MIN -10
#define MANUAL_MAX 110

struct TargetCurve {
    double a4, a3, a2, a1;
};

class Sensor {
   public:
    Sensor(uint16_t rawMinValue, uint16_t rawMaxValue, double minValue, double maxValue, double margin);
    virtual void read() {}
    virtual void update(uint16_t raw);
    void setConversion(double minValue, double maxValue);
    void setConversion();
    void setRawMin(uint16_t val);
    void setRawMax(uint16_t val);
    uint16_t setCurrentValRawMin();
    uint16_t setCurrentValRawMax();
    double convertedValue() const;
    bool isInRange() const;
    double getMaxValue() const;
    double getMinValue() const;
    uint16_t getRawValue() const;

   protected:
    MovingAverage<60> mvgAvg;
    uint16_t rawValue;
    uint16_t rawMinValue, rawMaxValue;
    const double minValue, maxValue;
    const double margin;
    double intercept, slope;
};

class Apps : public Sensor {
   public:
    Apps(uint16_t rawMinValue,
         uint16_t rawMaxValue,
         double minValue = APPS_MIN,
         double maxValue = APPS_MAX,
         double margin = APPS_MARGIN);
    double constrainedValue() const;
};

class Tps : public Sensor {
   public:
    Tps(uint16_t rawMinValue,
        uint16_t rawMaxValue,
        double minValue = TPS_MIN,
        double maxValue = TPS_MAX,
        double largeOpenThreshold = TPS_LARGE_OPEN_THRESHOLD,
        double margin = TPS_MARGIN);
    bool isLargeOpen() const;

   private:
    const double largeOpenThreshold;
};

class Ittr : public Apps {
   public:
    Ittr(uint16_t rawMinValue = 0,
         uint16_t rawMaxValue = 65535,
         double minValue = APPS_MIN,
         double maxValue = APPS_MAX,
         double margin = ITTR_MARGIN);
};

class Bps : public Sensor {
   public:
    Bps(uint16_t rawMinValue = BPS_RAW_MIN,
        uint16_t rawMaxValue = BPS_RAW_MAX,
        double minValue = BPS_MIN,
        double maxValue = BPS_MAX,
        double highPressureThreshold = BPS_HIGH_PRESSURE_THRESHOLD,
        double margin = BPS_MARGIN);
    bool isHighPressure() const;

   private:
    const double highPressureThreshold;
};

class ClutchSensor : public Sensor {
   public:
    ClutchSensor(uint16_t rawMinValue = 0,
                 uint16_t rawMaxValue = 65535,
                 double minValue = CLUTCH_MIN,
                 double maxValue = CLUTCH_MAX,
                 double margin = CLUTCH_MARGIN);
};

class GearPositionSensor {
   public:
    void update(uint16_t raw);
    int8_t getGear() const;
    uint16_t getRawValue() const;
    void setTable(uint8_t count, const int8_t gears[], const uint16_t rawValues[]);
    uint16_t setCurrentAsGear(int8_t gear);

   private:
    MovingAverage<60> mvgAvg_;
    uint16_t rawValue_ = 0;
    uint8_t gearCount_ = 0;
    int8_t gears_[GPS_MAX_GEARS] = {};
    uint16_t rawValues_[GPS_MAX_GEARS] = {};
};

class EtcTarget {
   public:
    enum class Mode { Calibration, Normal, Restricted, MotorOff };

    EtcTarget(Apps& apps, Ittr& ittr);
    double getTarget() const;
    void setModeCalibration();
    void setModeNormal();
    void setModeRestricted();
    void setModeMotorOff();
    Mode getMode() const { return mode; }
    bool isIttr() const;
    void setIttr(bool isIttr);
    void setIdlingValue(double val);
    void setNormalMaxValue(double val);
    void setRestrictedMaxValue(double val);
    const char* getModeString() const;

    uint16_t getSensorRawValue() const;
    double getSensorValue() const;

    bool setManual();
    bool isManual() const;
    double manualAdjust(double amount);
    void setTargetCurve(const TargetCurve& curve);

   private:
    Mode mode = Mode::Normal;
    Apps& apps;
    Ittr& ittr;
    bool _isIttr;
    double idlingValue;
    double minValue, maxValue;
    double manualTarget;
    bool _isManual = false;

    double normalMaxValue = 0.0;
    double restrictedMaxValue = 0.0;
    double tpsMinValue = TPS_MIN;
    double tpsMaxValue = TPS_MAX;

    double ca4 = 0.0;
    double ca3 = 0.0;
    double ca2 = 0.0;
    double ca1 = 0.0;
};
