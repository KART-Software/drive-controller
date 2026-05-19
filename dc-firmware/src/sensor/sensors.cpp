#include "sensors.hpp"

#include <string.h>

Sensor::Sensor(uint16_t rawMinValue, uint16_t rawMaxValue, double minValue, double maxValue, double margin)
    : rawMinValue(rawMinValue), rawMaxValue(rawMaxValue), minValue(minValue), maxValue(maxValue), margin(margin) {
    setConversion(minValue, maxValue);
}

void Sensor::setRawMin(uint16_t val) {
    rawMinValue = val;
    setConversion();
}

void Sensor::setRawMax(uint16_t val) {
    rawMaxValue = val;
    setConversion();
}

uint16_t Sensor::setCurrentValRawMin() {
    rawMinValue = rawValue;
    setConversion();
    return rawMinValue;
}

uint16_t Sensor::setCurrentValRawMax() {
    rawMaxValue = rawValue;
    setConversion();
    return rawMaxValue;
}

void Sensor::setConversion(double minValue, double maxValue) {
    this->slope = (maxValue - minValue) / (rawMaxValue - rawMinValue);
    this->intercept = (rawMaxValue * minValue - rawMinValue * maxValue) / (rawMaxValue - rawMinValue);
}

void Sensor::setConversion() {
    this->slope = (maxValue - minValue) / (rawMaxValue - rawMinValue);
    this->intercept = (rawMaxValue * minValue - rawMinValue * maxValue) / (rawMaxValue - rawMinValue);
}

double Sensor::convertedValue() const {
    double raw = mvgAvg.getAvg();
    return raw * slope + intercept;
}

bool Sensor::isInRange() const {
    double value = convertedValue();
    if (value < minValue - margin || value > maxValue + margin) {
        return false;
    }
    return true;
}

double Sensor::getMaxValue() const {
    return maxValue;
}

double Sensor::getMinValue() const {
    return minValue;
}

uint16_t Sensor::getRawValue() const {
    return rawValue;
}

void Sensor::update(uint16_t raw) {
    rawValue = raw;
    mvgAvg.add(rawValue);
}

Apps::Apps(uint16_t rawMinValue, uint16_t rawMaxValue, double minValue, double maxValue, double margin)
    : Sensor(rawMinValue, rawMaxValue, minValue, maxValue, margin) {}

double Apps::constrainedValue() const {
    return constrain(convertedValue(), minValue, maxValue);
}

Tps::Tps(uint16_t rawMinValue,
         uint16_t rawMaxValue,
         double minValue,
         double maxValue,
         double largeOpenThreshold,
         double margin)
    : Sensor(rawMinValue, rawMaxValue, minValue, maxValue, margin), largeOpenThreshold(largeOpenThreshold) {}

bool Tps::isLargeOpen() const {
    return convertedValue() > largeOpenThreshold;
}

Ittr::Ittr(uint16_t rawMinValue, uint16_t rawMaxValue, double minValue, double maxValue, double margin)
    : Apps(rawMinValue, rawMaxValue, minValue, maxValue, margin) {}

Bps::Bps(uint16_t rawMinValue,
         uint16_t rawMaxValue,
         double minValue,
         double maxValue,
         double highPressureThreshold,
         double margin)
    : Sensor(rawMinValue, rawMaxValue, minValue, maxValue, margin), highPressureThreshold(highPressureThreshold) {}

bool Bps::isHighPressure() const {
    return convertedValue() > highPressureThreshold;
}

// ── GearPositionSensor ──

ClutchSensor::ClutchSensor(uint16_t rawMinValue, uint16_t rawMaxValue, double minValue, double maxValue, double margin)
    : Sensor(rawMinValue, rawMaxValue, minValue, maxValue, margin) {}

void GearPositionSensor::update(uint16_t raw) {
    rawValue_ = raw;
    mvgAvg_.add(raw);
}

int8_t GearPositionSensor::getGear() const {
    if (gearCount_ == 0)
        return -1;
    uint16_t avg = (uint16_t)mvgAvg_.getAvg();
    int8_t best = -1;
    uint32_t bestDist = UINT32_MAX;
    for (uint8_t i = 0; i < gearCount_; i++) {
        uint32_t dist = (avg > rawValues_[i]) ? (avg - rawValues_[i]) : (rawValues_[i] - avg);
        if (dist < bestDist) {
            bestDist = dist;
            best = gears_[i];
        }
    }
    return (bestDist <= GPS_TOLERANCE) ? best : -1;
}

uint16_t GearPositionSensor::getRawValue() const {
    return rawValue_;
}

void GearPositionSensor::setTable(uint8_t count, const int8_t gears[], const uint16_t rawValues[]) {
    gearCount_ = (count > GPS_MAX_GEARS) ? GPS_MAX_GEARS : count;
    memcpy(gears_, gears, gearCount_ * sizeof(int8_t));
    memcpy(rawValues_, rawValues, gearCount_ * sizeof(uint16_t));
}

uint16_t GearPositionSensor::setCurrentAsGear(int8_t gear) {
    uint16_t avg = (uint16_t)mvgAvg_.getAvg();
    for (uint8_t i = 0; i < gearCount_; i++) {
        if (gears_[i] == gear) {
            rawValues_[i] = avg;
            return avg;
        }
    }
    return avg;
}

EtcTarget::EtcTarget(Apps& apps, Ittr& ittr) : apps(apps), ittr(ittr) {}

bool EtcTarget::isIttr() const {
    return _isIttr;
}

void EtcTarget::setIttr(bool isIttr) {
    _isIttr = isIttr;
}

double EtcTarget::getTarget() const {
    if (_isManual) {
        return manualTarget;
    }
    double x;
    if (_isIttr) {
        x = ittr.constrainedValue();
    } else {
        x = apps.constrainedValue();
    }
    // double y = -0.0000007403 * x * x * x * x + 0.0001425457 * x * x * x + 0.0025399794 * x * x + 0.0608039592 * x; //
    // TODO change
    double y = ((((ca4 * x + ca3) * x + ca2) * x + ca1) * x);
    return minValue + y * (maxValue - minValue) / 100.0;  // TODO change
}

void EtcTarget::setModeCalibration() {
    mode = Mode::Calibration;
    minValue = tpsMinValue;
    maxValue = tpsMaxValue;
}

void EtcTarget::setModeNormal() {
    mode = Mode::Normal;
    minValue = idlingValue;
    maxValue = normalMaxValue;
}

void EtcTarget::setModeRestricted() {
    mode = Mode::Restricted;
    minValue = idlingValue;
    maxValue = restrictedMaxValue;
}

void EtcTarget::setModeMotorOff() {
    mode = Mode::MotorOff;
    // min/max は意味を持たないが、getTarget() が呼ばれた場合に備えて idling 相当に
    minValue = idlingValue;
    maxValue = idlingValue;
}

void EtcTarget::setIdlingValue(double val) {
    idlingValue = val;
}

void EtcTarget::setNormalMaxValue(double val) {
    normalMaxValue = val;
}

void EtcTarget::setRestrictedMaxValue(double val) {
    restrictedMaxValue = val;
}

uint16_t EtcTarget::getSensorRawValue() const {
    if (_isIttr) {
        return ittr.getRawValue();
    } else {
        return apps.getRawValue();
    }
}

double EtcTarget::getSensorValue() const {
    if (_isIttr) {
        return ittr.convertedValue();
    } else {
        return apps.convertedValue();
    }
}

const char* EtcTarget::getModeString() const {
    switch (mode) {
        case EtcTarget::Mode::Calibration:
            return "Calib";
        case EtcTarget::Mode::Normal:
            return "Normal";
        case EtcTarget::Mode::Restricted:
            return "Restrict";
        case EtcTarget::Mode::MotorOff:
            return "MotorOff";
    }
    return "";
}

bool EtcTarget::setManual() {
    if (!_isManual) {
        manualTarget = ((int)(getTarget() * 10.0)) * 0.1;  // xx.x の値に丸める
    }
    _isManual = !_isManual;
    return _isManual;
}

bool EtcTarget::isManual() const {
    return _isManual;
}

double EtcTarget::manualAdjust(double amount) {
    manualTarget += amount;
    if (manualTarget < MANUAL_MIN)
        manualTarget = MANUAL_MIN;
    if (manualTarget > MANUAL_MAX)
        manualTarget = MANUAL_MAX;
    return manualTarget;
}

void EtcTarget::setTargetCurve(const TargetCurve& curve) {
    ca4 = curve.a4;
    ca3 = curve.a3;
    ca2 = curve.a2;
    ca1 = curve.a1;
}