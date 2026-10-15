#include "plausibility_validator.hpp"

namespace etc {

PlausibilityValidator::PlausibilityValidator(const Apps& apps1,
                                             const Apps& apps2,
                                             const Ittr& ittr,
                                             const Tps& tps1,
                                             const Tps& tps2,
                                             const EtcTarget& target,
                                             const Bps& bps)
    : apps1(apps1), apps2(apps2), ittr(ittr), tps1(tps1), tps2(tps2), target(target), bps(bps) {}

void PlausibilityValidator::initialize(uint32_t now) {
    initParameters(now);
}

void PlausibilityValidator::initParameters(uint32_t now) {
    lastAppsPlausibleTime = now;
    lastTpsPlausibleTime = now;
    lastTps1CircuitValidTime = now;
    lastTps2CircuitValidTime = now;
    lastApps1CircuitValidTime = now;
    lastApps2CircuitValidTime = now;
    lastAppsTpsTargetValidTime = now;
    lastBpsCircuitValidTime = now;
    lastBpsTpsPlausibleTime = now;
    isValidAllTime = true;
}

bool PlausibilityValidator::isCurrentlyValid(uint32_t now) {
    if (now < PLAUSIBLE_DURATION) {
        isValidAllTime = true;
        lastCurrentlyValid_ = true;
        return true;
    } else {
        bool isValid = true;
        isValid &= isAppsPlausible(now) || !appsCheckFlag;
        isValid &= isTpsPlausible(now) || !tpsCheckFlag;
        isValid &= isApps1CircuitValid(now) || !apps1CheckFlag;
        isValid &= isApps2CircuitValid(now) || !apps2CheckFlag;
        isValid &= isTps1CircuitValid(now) || !tps1CheckFlag;
        isValid &= isTps2CircuitValid(now) || !tps2CheckFlag;
        // 一時停止中 (同定実験) は判定自体を呼ばない: 左辺評価で ERR_APPS_TPS_TARGET_FAILURE が立ち、
        // 二度と消えない偽エラーになるため
        if (!targetCheckSuspended_)
            isValid &= isAppsTpsTargetValid(now) || !targetCheckFlag;
        isValid &= isBpsCircuitValid(now) || !bpsCheckFlag;
        isValid &= isBpsTpsPlausible(now) || !bpsTpsCheckFlag;

        isValidAllTime &= isValid;
        lastCurrentlyValid_ = isValid;
        return isValid;
    }
}

void PlausibilityValidator::suspendTargetCheck(bool suspend, uint32_t now) {
    if (!suspend && targetCheckSuspended_)
        lastAppsTpsTargetValidTime = now;  // 再開: 停止中の乖離を持ち越さない
    targetCheckSuspended_ = suspend;
}

bool PlausibilityValidator::isValid(uint32_t now) {
    isValidAllTime &= isCurrentlyValid(now);
    return isValidAllTime;
}

bool PlausibilityValidator::isAppsPlausible(uint32_t now) {
    if (abs(apps1.convertedValue() - apps2.convertedValue()) < SENSOR_SAME_POSITION_THRESHOLD) {
        lastAppsPlausibleTime = now;
        return true;
    }
    if (now - lastAppsPlausibleTime > SENSOR_IMPLAUSIBLE_THRESHOLD_TIME) {
        errorHandler.raise(ERR_APPS_IMPLAUSIBLE);
        return false;
    }
    return true;
}

bool PlausibilityValidator::isTpsPlausible(uint32_t now) {
    if (abs(tps1.convertedValue() - tps2.convertedValue()) < SENSOR_SAME_POSITION_THRESHOLD) {
        lastTpsPlausibleTime = now;
        return true;
    }
    if (now - lastTpsPlausibleTime > SENSOR_IMPLAUSIBLE_THRESHOLD_TIME) {
        errorHandler.raise(ERR_TPS_IMPLAUSIBLE);
        return false;
    }
    return true;
}

bool PlausibilityValidator::isApps1CircuitValid(uint32_t now) {
    if (apps1.isInRange()) {
        lastApps1CircuitValidTime = now;
        return true;
    }
    if (now - lastApps1CircuitValidTime > SENSOR_IMPLAUSIBLE_THRESHOLD_TIME) {
        errorHandler.raise(ERR_APPS_1_CIRCUIT_FAILURE);
        return false;
    }
    return true;
}

bool PlausibilityValidator::isApps2CircuitValid(uint32_t now) {
    if (apps2.isInRange()) {
        lastApps2CircuitValidTime = now;
        return true;
    }
    if (now - lastApps2CircuitValidTime > SENSOR_IMPLAUSIBLE_THRESHOLD_TIME) {
        errorHandler.raise(ERR_APPS_2_CIRCUIT_FAILURE);
        return false;
    }
    return true;
}

bool PlausibilityValidator::isTps1CircuitValid(uint32_t now) {
    if (tps1.isInRange()) {
        lastTps1CircuitValidTime = now;
        return true;
    }
    if (now - lastTps1CircuitValidTime > SENSOR_IMPLAUSIBLE_THRESHOLD_TIME) {
        errorHandler.raise(ERR_TPS_1_CIRCUIT_FAILURE);
        return false;
    }
    return true;
}

bool PlausibilityValidator::isTps2CircuitValid(uint32_t now) {
    if (tps2.isInRange()) {
        lastTps2CircuitValidTime = now;
        return true;
    }
    if (now - lastTps2CircuitValidTime > SENSOR_IMPLAUSIBLE_THRESHOLD_TIME) {
        errorHandler.raise(ERR_TPS_2_CIRCUIT_FAILURE);
        return false;
    }
    return true;
}

bool PlausibilityValidator::isAppsTpsTargetValid(uint32_t now) {
    double targetTp = target.getTarget();
    double tpsValue = tps1.convertedValue();
    if (abs(targetTp - tpsValue) < SENSOR_SAME_POSITION_THRESHOLD) {
        lastAppsTpsTargetValidTime = now;
        return true;
    }
    if (now - lastAppsTpsTargetValidTime > APPS_TPS_TARGET_IMPLAUSIBLE_THRESHOLD_TIME) {
        errorHandler.raise(ERR_APPS_TPS_TARGET_FAILURE);
        return false;
    }
    return true;
}

bool PlausibilityValidator::isBpsCircuitValid(uint32_t now) {
    if (bps.isInRange()) {
        lastBpsCircuitValidTime = now;
        return true;
    }
    if (now - lastBpsCircuitValidTime > SENSOR_IMPLAUSIBLE_THRESHOLD_TIME) {
        errorHandler.raise(ERR_BPS_CIRCUIT_FAILURE);
        return false;
    }
    return true;
}

bool PlausibilityValidator::isBpsTpsPlausible(uint32_t now) {
    if (!bps.isHighPressure() || !tps1.isLargeOpen()) {
        lastBpsTpsPlausibleTime = now;
        return true;
    }
    if (now - lastBpsTpsPlausibleTime > BPS_TPS_IMPLAUSIBLE_THRESHOLD_TIME) {
        errorHandler.raise(ERR_BPS_TPS_IMPLAUSIBLE);
        return false;
    }
    return true;
}

void PlausibilityValidator::setCheckFlags(bool apps,
                                          bool tps,
                                          bool apps1,
                                          bool apps2,
                                          bool tps1,
                                          bool tps2,
                                          bool target,
                                          bool bps,
                                          bool bpsTps) {
    appsCheckFlag = apps;
    tpsCheckFlag = tps;
    apps1CheckFlag = apps1;
    apps2CheckFlag = apps2;
    tps1CheckFlag = tps1;
    tps2CheckFlag = tps2;
    targetCheckFlag = target;
    bpsCheckFlag = bps;
    bpsTpsCheckFlag = bpsTps;
}

}  // namespace etc
