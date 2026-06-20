#include "configurator.hpp"
#include "default_config.hpp"
#include "serial/serial_protocol.hpp"

Configurator::Configurator(Flash& flash,
                           SensorHub& hub,
                           etc::MotorController& motorController,
                           etc::PlausibilityValidator& plausibilityValidator,
                           launch::LaunchController& launchController,
                           shift::AutoShifter& autoShifter)
    : flash(flash),
      apps1(hub.mut.apps1()),
      apps2(hub.mut.apps2()),
      tps1(hub.mut.tps1()),
      tps2(hub.mut.tps2()),
      ittr(hub.mut.ittr()),
      target(hub.mut.target()),
      gps(hub.mut.gps()),
      clutch(hub.mut.clutch()),
      motorController(motorController),
      plausibilityValidator(plausibilityValidator),
      launchController(launchController),
      autoShifter(autoShifter) {}

void Configurator::loadDefault() {
    config = DEFAULT_CONFIG;
}

void Configurator::calibrate() {
    if (!config.has_sensor_calib)
        return;
    const dc_SensorCalib& sv = config.sensor_calib;
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

    if (config.has_etc) {
        const dc_EtcConfig& ec = config.etc;
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

    if (config.has_sensor_calib) {
        const dc_GpsCalib& gc = config.sensor_calib.gps;
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

    if (config.has_launch) {
        const dc_SensorCalib& sc = config.sensor_calib;
        launchController.setConfig(config.launch, sc.engine_teeth, sc.wheel_rotor_teeth_front,
                                   sc.wheel_rotor_teeth_rear);
    }

    if (config.has_auto_shift) {
        const dc_SensorCalib& sc = config.sensor_calib;
        autoShifter.setConfig(config.auto_shift, sc.gps.type, sc.engine_teeth);
    }
}

void Configurator::overlayConfig(const dc_Config& src) {
    if (src.has_sensor_calib)
        config.sensor_calib = src.sensor_calib;
    if (src.has_etc)
        config.etc = src.etc;
    if (src.has_launch)
        config.launch = src.launch;
    if (src.has_auto_shift)
        config.auto_shift = src.auto_shift;
    // 新トップレベルメッセージ (例: traction) はここに追加
}

void Configurator::loadConfigFromFlash() {
    loadDefault();
    dc_Config loaded = dc_Config_init_zero;
    if (!flash.readProto(CONFIG_FILE_NAME, dc_Config_fields, &loaded))
        return;
    overlayConfig(loaded);
}

void Configurator::calibrateFromFlash() {
    loadConfigFromFlash();
    calibrate();
}

void Configurator::setAppsMin() {
    config.sensor_calib.apps1_min = apps1.setCurrentValRawMin();
    config.sensor_calib.apps2_min = apps2.setCurrentValRawMin();
    config.sensor_calib.ittr_min = ittr.setCurrentValRawMin();
    configChanged = true;
}

void Configurator::setAppsMax() {
    config.sensor_calib.apps1_max = apps1.setCurrentValRawMax();
    config.sensor_calib.apps2_max = apps2.setCurrentValRawMax();
    config.sensor_calib.ittr_max = ittr.setCurrentValRawMax();
    configChanged = true;
}

void Configurator::setTpsMin() {
    config.sensor_calib.tps1_min = tps1.setCurrentValRawMin();
    config.sensor_calib.tps2_min = tps2.setCurrentValRawMin();
    configChanged = true;
}

void Configurator::setTpsMax() {
    config.sensor_calib.tps1_max = tps1.setCurrentValRawMax();
    config.sensor_calib.tps2_max = tps2.setCurrentValRawMax();
    configChanged = true;
}

void Configurator::setIdling() {
    config.sensor_calib.target_tp_idling = (float)tps1.convertedValue();
    target.setIdlingValue(config.sensor_calib.target_tp_idling);
    configChanged = true;
}

void Configurator::setClutchMin() {
    config.sensor_calib.clutch_min = clutch.setCurrentValRawMin();
    configChanged = true;
}

void Configurator::setClutchMax() {
    config.sensor_calib.clutch_max = clutch.setCurrentValRawMax();
    configChanged = true;
}

void Configurator::setTargetBound(float idling, float normalMax, float restrictedMax) {
    config.sensor_calib.target_tp_idling = idling;
    config.sensor_calib.target_tp_normal_max = normalMax;
    config.sensor_calib.target_tp_restricted_max = restrictedMax;
    target.setIdlingValue(idling);
    target.setNormalMaxValue(normalMax);
    target.setRestrictedMaxValue(restrictedMax);
    configChanged = true;
}

void Configurator::setPlausibilityFlags(const dc_EtcPlausibilityCheckFlags& flags) {
    config.etc.plausibility_check_flags = flags;
    config.etc.has_plausibility_check_flags = true;
    plausibilityValidator.setCheckFlags(flags.apps, flags.tps, flags.apps1, flags.apps2, flags.tps1, flags.tps2,
                                        flags.target, flags.bps, flags.bps_tps);
    configChanged = true;
}

void Configurator::setIttrFlag(bool val) {
    config.etc.use_ittr = val;
    target.setIttr(val);
    configChanged = true;
}

void Configurator::setPid(float kP, float kI, float kD) {
    config.etc.pid.k_p = kP;
    config.etc.pid.k_i = kI;
    config.etc.pid.k_d = kD;
    config.etc.has_pid = true;
    motorController.setPidGains((double)kP, (double)kI, (double)kD);
    configChanged = true;
}

void Configurator::setTargetCurve(const dc_EtcTargetCurve& curve) {
    config.etc.target_curve = curve;
    config.etc.has_target_curve = true;
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
    dc_GpsCalib& gc = config.sensor_calib.gps;
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
    // 部分 Config が渡されたときに既存設定をゼロで上書きしないよう has_* チェック経由で反映
    overlayConfig(cfg);
    calibrate();
    configChanged = true;
    save();
    return true;
}

void Configurator::save() {
    if (configChanged) {
        flash.writeProto(CONFIG_FILE_NAME, dc_Config_fields, &config);
        SerialProtocol::sendDebugf("save: normalMax=%g restrictedMax=%g",
                                   (double)config.sensor_calib.target_tp_normal_max,
                                   (double)config.sensor_calib.target_tp_restricted_max);
    }
    configChanged = false;
}

void Configurator::revert() {
    calibrateFromFlash();
    configChanged = false;
}
