#include "configurator.hpp"
#include "serial/serial_protocol.hpp"

Configurator::Configurator(SensorHub& hub,
                           MotorController& motorController,
                           PlausibilityValidator& plausibilityValidator)
    : apps1(hub.mut.apps1()),
      apps2(hub.mut.apps2()),
      tps1(hub.mut.tps1()),
      tps2(hub.mut.tps2()),
      ittr(hub.mut.ittr()),
      target(hub.mut.target()),
      motorController(motorController),
      plausibilityValidator(plausibilityValidator) {}

void Configurator::initialize() {
    flash.initialize();
    // TODO initialize 失敗時の処理
}

void Configurator::calibrate() {
    apps1.setRawMin(config.sensorValues.apps1Min);
    apps1.setRawMax(config.sensorValues.apps1Max);
    apps2.setRawMin(config.sensorValues.apps2Min);
    apps2.setRawMax(config.sensorValues.apps2Max);
    ittr.setRawMin(config.sensorValues.ittrMin);
    ittr.setRawMax(config.sensorValues.ittrMax);
    target.setIdlingValue(config.sensorValues.idling);
    target.setNormalMaxValue(config.sensorValues.normalMax);
    target.setRestrictedMaxValue(config.sensorValues.restrictedMax);
    tps1.setRawMin(config.sensorValues.tps1Min);
    tps1.setRawMax(config.sensorValues.tps1Max);
    tps2.setRawMin(config.sensorValues.tps2Min);
    tps2.setRawMax(config.sensorValues.tps2Max);
    plausibilityValidator.setCheckFlags(
        config.plausibilityFlags.apps, config.plausibilityFlags.tps, config.plausibilityFlags.apps1,
        config.plausibilityFlags.apps2, config.plausibilityFlags.tps1, config.plausibilityFlags.tps2,
        config.plausibilityFlags.target, config.plausibilityFlags.bps, config.plausibilityFlags.bpsTps);
    target.setIttr(config.useIttr);
    motorController.setPidGains(config.pid.kP, config.pid.kI, config.pid.kD);
    target.setTargetCurve(config.targetCurve);
}

void Configurator::loadConfigFromFlash() {
    String jsonStr = flash.read(CONFIG_FILE_NAME);
    if (!config.loadFromJson(jsonStr)) {
        config.loadFromConstants();
    }
}

void Configurator::calibrateFromFlash() {
    loadConfigFromFlash();
    calibrate();
}

void Configurator::setPlausibilityFlags(const PlausibilityCheckFlags& flags) {
    config.plausibilityFlags = flags;
    plausibilityValidator.setCheckFlags(flags.apps, flags.tps, flags.apps1, flags.apps2, flags.tps1, flags.tps2,
                                        flags.target, flags.bps, flags.bpsTps);
    configChanged = true;
}

void Configurator::setIttrFlag(bool val) {
    config.useIttr = val;
    target.setIttr(val);
    configChanged = true;
}

void Configurator::setPid(double kP, double kI, double kD) {
    config.pid.kP = kP;
    config.pid.kI = kI;
    config.pid.kD = kD;
    motorController.setPidGains(kP, kI, kD);
    configChanged = true;
}

void Configurator::setTargetCurve(const TargetCurve& curve) {
    config.targetCurve = curve;
    target.setTargetCurve(curve);
    configChanged = true;
}

bool Configurator::importConfig(const dc_Config& cfg) {
    ConfigModel tmp = config;
    if (cfg.has_sensor_values) {
        tmp.sensorValues.apps1Min = cfg.sensor_values.apps1_min;
        tmp.sensorValues.apps1Max = cfg.sensor_values.apps1_max;
        tmp.sensorValues.apps2Min = cfg.sensor_values.apps2_min;
        tmp.sensorValues.apps2Max = cfg.sensor_values.apps2_max;
        tmp.sensorValues.ittrMin = cfg.sensor_values.ittr_min;
        tmp.sensorValues.ittrMax = cfg.sensor_values.ittr_max;
        tmp.sensorValues.tps1Min = cfg.sensor_values.tps1_min;
        tmp.sensorValues.tps1Max = cfg.sensor_values.tps1_max;
        tmp.sensorValues.tps2Min = cfg.sensor_values.tps2_min;
        tmp.sensorValues.tps2Max = cfg.sensor_values.tps2_max;
        tmp.sensorValues.idling = cfg.sensor_values.target_tp_idling;
        tmp.sensorValues.normalMax = cfg.sensor_values.target_tp_normal_max;
        tmp.sensorValues.restrictedMax = cfg.sensor_values.target_tp_restricted_max;
    }
    if (cfg.has_etc_config) {
        const dc_EtcConfig& ec = cfg.etc_config;
        if (ec.has_plausibility_check_flags) {
            tmp.plausibilityFlags.apps = ec.plausibility_check_flags.apps;
            tmp.plausibilityFlags.tps = ec.plausibility_check_flags.tps;
            tmp.plausibilityFlags.apps1 = ec.plausibility_check_flags.apps1;
            tmp.plausibilityFlags.apps2 = ec.plausibility_check_flags.apps2;
            tmp.plausibilityFlags.tps1 = ec.plausibility_check_flags.tps1;
            tmp.plausibilityFlags.tps2 = ec.plausibility_check_flags.tps2;
            tmp.plausibilityFlags.target = ec.plausibility_check_flags.target;
            tmp.plausibilityFlags.bps = ec.plausibility_check_flags.bps;
            tmp.plausibilityFlags.bpsTps = ec.plausibility_check_flags.bps_tps;
        }
        tmp.useIttr = ec.use_ittr;
        if (ec.has_pid) {
            tmp.pid.kP = ec.pid.k_p;
            tmp.pid.kI = ec.pid.k_i;
            tmp.pid.kD = ec.pid.k_d;
        }
        if (ec.has_target_curve) {
            tmp.targetCurve.a4 = ec.target_curve.a4;
            tmp.targetCurve.a3 = ec.target_curve.a3;
            tmp.targetCurve.a2 = ec.target_curve.a2;
            tmp.targetCurve.a1 = ec.target_curve.a1;
        }
    }
    config = tmp;
    calibrate();
    configChanged = true;
    save();
    return true;
}

void Configurator::setAppsMin() {
    config.sensorValues.apps1Min = apps1.setCurrentValRawMin();
    config.sensorValues.apps2Min = apps2.setCurrentValRawMin();
    config.sensorValues.ittrMin = ittr.setCurrentValRawMin();
    configChanged = true;
}

void Configurator::setAppsMax() {
    config.sensorValues.apps1Max = apps1.setCurrentValRawMax();
    config.sensorValues.apps2Max = apps2.setCurrentValRawMax();
    config.sensorValues.ittrMax = ittr.setCurrentValRawMax();
    configChanged = true;
}

void Configurator::setTpsMin() {
    config.sensorValues.tps1Min = tps1.setCurrentValRawMin();
    config.sensorValues.tps2Min = tps2.setCurrentValRawMin();
    configChanged = true;
}

void Configurator::setTpsMax() {
    config.sensorValues.tps1Max = tps1.setCurrentValRawMax();
    config.sensorValues.tps2Max = tps2.setCurrentValRawMax();
    configChanged = true;
}

void Configurator::setIdling() {
    config.sensorValues.idling = tps1.convertedValue();
    target.setIdlingValue(config.sensorValues.idling);
    configChanged = true;
}

void Configurator::setTargetBound(double idling, double normalMax, double restrictedMax) {
    config.sensorValues.idling = idling;
    config.sensorValues.normalMax = normalMax;
    config.sensorValues.restrictedMax = restrictedMax;
    target.setIdlingValue(idling);
    target.setNormalMaxValue(normalMax);
    target.setRestrictedMaxValue(restrictedMax);
    configChanged = true;
}

void Configurator::save() {
    if (configChanged) {
        StaticJsonDocument<CONFIG_JSON_SIZE> doc;
        JsonObject root = doc.to<JsonObject>();
        config.toJson(root);
        if (doc.overflowed()) {
            SerialProtocol::sendDebugf("WARN: save doc overflowed (used %u/%u)", (unsigned)doc.memoryUsage(),
                                       (unsigned)CONFIG_JSON_SIZE);
        }
        SerialProtocol::sendDebugf("save: normalMax=%g restrictedMax=%g mem=%u/%u", config.sensorValues.normalMax,
                                   config.sensorValues.restrictedMax, (unsigned)doc.memoryUsage(),
                                   (unsigned)CONFIG_JSON_SIZE);
        String out;
        serializeJson(doc, out);
        flash.write(CONFIG_FILE_NAME, out);
    }
    configChanged = false;
}

void Configurator::revert() {
    calibrateFromFlash();
    configChanged = false;
}

void Configurator::getConfigJson(JsonObject& out) {
    config.toJson(out);
    out["configChanged"] = configChanged;
}

void Configurator::getConfigProto(dc_Config& out) {
    out = dc_Config_init_zero;

    out.has_sensor_values = true;
    out.sensor_values.apps1_min = config.sensorValues.apps1Min;
    out.sensor_values.apps1_max = config.sensorValues.apps1Max;
    out.sensor_values.apps2_min = config.sensorValues.apps2Min;
    out.sensor_values.apps2_max = config.sensorValues.apps2Max;
    out.sensor_values.ittr_min = config.sensorValues.ittrMin;
    out.sensor_values.ittr_max = config.sensorValues.ittrMax;
    out.sensor_values.tps1_min = config.sensorValues.tps1Min;
    out.sensor_values.tps1_max = config.sensorValues.tps1Max;
    out.sensor_values.tps2_min = config.sensorValues.tps2Min;
    out.sensor_values.tps2_max = config.sensorValues.tps2Max;
    out.sensor_values.target_tp_idling = config.sensorValues.idling;
    out.sensor_values.target_tp_normal_max = config.sensorValues.normalMax;
    out.sensor_values.target_tp_restricted_max = config.sensorValues.restrictedMax;

    out.has_etc_config = true;
    dc_EtcConfig& ec = out.etc_config;
    ec.has_plausibility_check_flags = true;
    ec.plausibility_check_flags.apps = config.plausibilityFlags.apps;
    ec.plausibility_check_flags.tps = config.plausibilityFlags.tps;
    ec.plausibility_check_flags.apps1 = config.plausibilityFlags.apps1;
    ec.plausibility_check_flags.apps2 = config.plausibilityFlags.apps2;
    ec.plausibility_check_flags.tps1 = config.plausibilityFlags.tps1;
    ec.plausibility_check_flags.tps2 = config.plausibilityFlags.tps2;
    ec.plausibility_check_flags.target = config.plausibilityFlags.target;
    ec.plausibility_check_flags.bps = config.plausibilityFlags.bps;
    ec.plausibility_check_flags.bps_tps = config.plausibilityFlags.bpsTps;

    ec.use_ittr = config.useIttr;

    ec.has_pid = true;
    ec.pid.k_p = config.pid.kP;
    ec.pid.k_i = config.pid.kI;
    ec.pid.k_d = config.pid.kD;

    ec.has_target_curve = true;
    ec.target_curve.a4 = config.targetCurve.a4;
    ec.target_curve.a3 = config.targetCurve.a3;
    ec.target_curve.a2 = config.targetCurve.a2;
    ec.target_curve.a1 = config.targetCurve.a1;

    out.config_changed = configChanged;
}