#include "configurator.hpp"
#include "serial/serial_protocol.hpp"

Configurator::Configurator(SensorHub& hub,
                           etc::MotorController& motorController,
                           etc::PlausibilityValidator& plausibilityValidator)
    : apps1(hub.mut.apps1()),
      apps2(hub.mut.apps2()),
      tps1(hub.mut.tps1()),
      tps2(hub.mut.tps2()),
      ittr(hub.mut.ittr()),
      target(hub.mut.target()),
      gps(hub.mut.gps()),
      clutch(hub.mut.clutch()),
      motorController(motorController),
      plausibilityValidator(plausibilityValidator) {}

void Configurator::initialize() {
    flash.initialize();
}

void Configurator::loadFromConstants() {
    config = dc_Config_init_zero;

    config.has_sensor_values = true;
    dc_SensorCalib& sv = config.sensor_values;
    sv.apps1_min = APPS_1_RAW_MIN;
    sv.apps1_max = APPS_1_RAW_MAX;
    sv.apps2_min = APPS_2_RAW_MIN;
    sv.apps2_max = APPS_2_RAW_MAX;
    sv.ittr_min = ITTR_RAW_MIN;
    sv.ittr_max = ITTR_RAW_MAX;
    sv.tps1_min = TPS_1_RAW_MIN;
    sv.tps1_max = TPS_1_RAW_MAX;
    sv.tps2_min = TPS_2_RAW_MIN;
    sv.tps2_max = TPS_2_RAW_MAX;
    sv.target_tp_idling = TARGET_IDLING;
    sv.target_tp_normal_max = TARGET_NORMAL_MAX;
    sv.target_tp_restricted_max = TARGET_RESTRICTED_MAX;
    sv.clutch_min = CLUTCH_RAW_MIN;
    sv.clutch_max = CLUTCH_RAW_MAX;

    config.has_etc_config = true;
    dc_EtcConfig& ec = config.etc_config;
    ec.has_plausibility_check_flags = true;
    ec.plausibility_check_flags.apps = APPS_CHECK_FLAG;
    ec.plausibility_check_flags.tps = TPS_CHECK_FLAG;
    ec.plausibility_check_flags.apps1 = APPS1_CHECK_FLAG;
    ec.plausibility_check_flags.apps2 = APPS2_CHECK_FLAG;
    ec.plausibility_check_flags.tps1 = TPS1_CHECK_FLAG;
    ec.plausibility_check_flags.tps2 = TPS2_CHECK_FLAG;
    ec.plausibility_check_flags.target = TARGET_CHECK_FLAG;
    ec.plausibility_check_flags.bps = BPS_CHECK_FLAG;
    ec.plausibility_check_flags.bps_tps = BPSTPS_CHECK_FLAG;
    ec.use_ittr = USE_ITTR;
    ec.has_pid = true;
    ec.pid.k_p = KP;
    ec.pid.k_i = KI;
    ec.pid.k_d = KD;
    ec.has_target_curve = true;
    ec.target_curve.a4 = TARGET_CURVE_A4;
    ec.target_curve.a3 = TARGET_CURVE_A3;
    ec.target_curve.a2 = TARGET_CURVE_A2;
    ec.target_curve.a1 = TARGET_CURVE_A1;

    config.has_gps_calib = true;
    dc_GpsCalib& gc = config.gps_calib;
    gc.type = USE_IST ? dc_TransmissionType_TRANSMISSION_IST : dc_TransmissionType_TRANSMISSION_NORMAL;
    const uint16_t istDef[] = GPS_IST_RAW_DEFAULTS;
    gc.ist_raw_values_count = GPS_IST_GEAR_COUNT;
    for (uint8_t i = 0; i < GPS_IST_GEAR_COUNT; i++)
        gc.ist_raw_values[i] = istDef[i];
    const uint16_t normDef[] = GPS_NORMAL_RAW_DEFAULTS;
    gc.normal_raw_values_count = GPS_NORMAL_GEAR_COUNT;
    for (uint8_t i = 0; i < GPS_NORMAL_GEAR_COUNT; i++)
        gc.normal_raw_values[i] = normDef[i];
}

void Configurator::calibrate() {
    if (!config.has_sensor_values)
        return;
    const dc_SensorCalib& sv = config.sensor_values;
    apps1.setRawMin(sv.apps1_min);
    apps1.setRawMax(sv.apps1_max);
    apps2.setRawMin(sv.apps2_min);
    apps2.setRawMax(sv.apps2_max);
    ittr.setRawMin(sv.ittr_min);
    ittr.setRawMax(sv.ittr_max);
    tps1.setRawMin(sv.tps1_min);
    tps1.setRawMax(sv.tps1_max);
    tps2.setRawMin(sv.tps2_min);
    tps2.setRawMax(sv.tps2_max);
    target.setIdlingValue(sv.target_tp_idling);
    target.setNormalMaxValue(sv.target_tp_normal_max);
    target.setRestrictedMaxValue(sv.target_tp_restricted_max);
    clutch.setRawMin(sv.clutch_min);
    clutch.setRawMax(sv.clutch_max);

    if (config.has_etc_config) {
        const dc_EtcConfig& ec = config.etc_config;
        if (ec.has_plausibility_check_flags) {
            const auto& f = ec.plausibility_check_flags;
            plausibilityValidator.setCheckFlags(f.apps, f.tps, f.apps1, f.apps2, f.tps1, f.tps2, f.target, f.bps,
                                                f.bps_tps);
        }
        target.setIttr(ec.use_ittr);
        if (ec.has_pid)
            motorController.setPidGains(ec.pid.k_p, ec.pid.k_i, ec.pid.k_d);
        if (ec.has_target_curve) {
            TargetCurve tc;
            tc.a4 = ec.target_curve.a4;
            tc.a3 = ec.target_curve.a3;
            tc.a2 = ec.target_curve.a2;
            tc.a1 = ec.target_curve.a1;
            target.setTargetCurve(tc);
        }
    }

    if (config.has_gps_calib) {
        const dc_GpsCalib& gc = config.gps_calib;
        if (gc.type == dc_TransmissionType_TRANSMISSION_IST) {
            static const int8_t gears[] = GPS_IST_GEARS;
            uint16_t raw[GPS_IST_GEAR_COUNT];
            for (uint8_t i = 0; i < GPS_IST_GEAR_COUNT; i++)
                raw[i] = (i < gc.ist_raw_values_count) ? (uint16_t)gc.ist_raw_values[i] : 32768;
            gps.setTable(GPS_IST_GEAR_COUNT, gears, raw);
        } else {
            static const int8_t gears[] = GPS_NORMAL_GEARS;
            uint16_t raw[GPS_NORMAL_GEAR_COUNT];
            for (uint8_t i = 0; i < GPS_NORMAL_GEAR_COUNT; i++)
                raw[i] = (i < gc.normal_raw_values_count) ? (uint16_t)gc.normal_raw_values[i] : 32768;
            gps.setTable(GPS_NORMAL_GEAR_COUNT, gears, raw);
        }
    }
}

void Configurator::loadConfigFromFlash() {
    loadFromConstants();
    dc_Config loaded = dc_Config_init_zero;
    if (!flash.readProto(CONFIG_FILE_NAME, dc_Config_fields, &loaded))
        return;
    if (loaded.has_sensor_values)
        config.sensor_values = loaded.sensor_values;
    if (loaded.has_etc_config)
        config.etc_config = loaded.etc_config;
    if (loaded.has_gps_calib)
        config.gps_calib = loaded.gps_calib;
}

void Configurator::calibrateFromFlash() {
    loadConfigFromFlash();
    calibrate();
}

void Configurator::setAppsMin() {
    config.sensor_values.apps1_min = apps1.setCurrentValRawMin();
    config.sensor_values.apps2_min = apps2.setCurrentValRawMin();
    config.sensor_values.ittr_min = ittr.setCurrentValRawMin();
    configChanged = true;
}

void Configurator::setAppsMax() {
    config.sensor_values.apps1_max = apps1.setCurrentValRawMax();
    config.sensor_values.apps2_max = apps2.setCurrentValRawMax();
    config.sensor_values.ittr_max = ittr.setCurrentValRawMax();
    configChanged = true;
}

void Configurator::setTpsMin() {
    config.sensor_values.tps1_min = tps1.setCurrentValRawMin();
    config.sensor_values.tps2_min = tps2.setCurrentValRawMin();
    configChanged = true;
}

void Configurator::setTpsMax() {
    config.sensor_values.tps1_max = tps1.setCurrentValRawMax();
    config.sensor_values.tps2_max = tps2.setCurrentValRawMax();
    configChanged = true;
}

void Configurator::setIdling() {
    config.sensor_values.target_tp_idling = (float)tps1.convertedValue();
    target.setIdlingValue(config.sensor_values.target_tp_idling);
    configChanged = true;
}

void Configurator::setTargetBound(float idling, float normalMax, float restrictedMax) {
    config.sensor_values.target_tp_idling = idling;
    config.sensor_values.target_tp_normal_max = normalMax;
    config.sensor_values.target_tp_restricted_max = restrictedMax;
    target.setIdlingValue(idling);
    target.setNormalMaxValue(normalMax);
    target.setRestrictedMaxValue(restrictedMax);
    configChanged = true;
}

void Configurator::setPlausibilityFlags(const dc_EtcPlausibilityCheckFlags& flags) {
    config.etc_config.plausibility_check_flags = flags;
    config.etc_config.has_plausibility_check_flags = true;
    plausibilityValidator.setCheckFlags(flags.apps, flags.tps, flags.apps1, flags.apps2, flags.tps1, flags.tps2,
                                        flags.target, flags.bps, flags.bps_tps);
    configChanged = true;
}

void Configurator::setIttrFlag(bool val) {
    config.etc_config.use_ittr = val;
    target.setIttr(val);
    configChanged = true;
}

void Configurator::setPid(float kP, float kI, float kD) {
    config.etc_config.pid.k_p = kP;
    config.etc_config.pid.k_i = kI;
    config.etc_config.pid.k_d = kD;
    config.etc_config.has_pid = true;
    motorController.setPidGains((double)kP, (double)kI, (double)kD);
    configChanged = true;
}

void Configurator::setTargetCurve(const dc_EtcTargetCurve& curve) {
    config.etc_config.target_curve = curve;
    config.etc_config.has_target_curve = true;
    TargetCurve tc;
    tc.a4 = curve.a4;
    tc.a3 = curve.a3;
    tc.a2 = curve.a2;
    tc.a1 = curve.a1;
    target.setTargetCurve(tc);
    configChanged = true;
}

void Configurator::setGpsGear(int8_t gear) {
    uint16_t raw = gps.setCurrentAsGear(gear);
    dc_GpsCalib& gc = config.gps_calib;
    if (gc.type == dc_TransmissionType_TRANSMISSION_IST) {
        const int8_t gears[] = GPS_IST_GEARS;
        for (uint8_t i = 0; i < GPS_IST_GEAR_COUNT; i++) {
            if (gears[i] == gear) {
                gc.ist_raw_values[i] = raw;
                break;
            }
        }
    } else {
        const int8_t gears[] = GPS_NORMAL_GEARS;
        for (uint8_t i = 0; i < GPS_NORMAL_GEAR_COUNT; i++) {
            if (gears[i] == gear) {
                gc.normal_raw_values[i] = raw;
                break;
            }
        }
    }
    configChanged = true;
}

bool Configurator::importConfig(const dc_Config& cfg) {
    config = cfg;
    calibrate();
    configChanged = true;
    save();
    return true;
}

void Configurator::save() {
    if (configChanged) {
        flash.writeProto(CONFIG_FILE_NAME, dc_Config_fields, &config);
        SerialProtocol::sendDebugf("save: normalMax=%g restrictedMax=%g",
                                   (double)config.sensor_values.target_tp_normal_max,
                                   (double)config.sensor_values.target_tp_restricted_max);
    }
    configChanged = false;
}

void Configurator::revert() {
    calibrateFromFlash();
    configChanged = false;
}
