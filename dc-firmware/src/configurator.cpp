#include "configurator.hpp"
#include "default_config.hpp"
#include "util/log/debug_logger.hpp"

Configurator::Configurator(Flash& flash,
                           SensorHub& hub,
                           etc::MotorController& motorController,
                           etc::PlausibilityValidator& plausibilityValidator,
                           launch::LaunchController& launchController,
                           shift::AutoShifter& autoShifter,
                           starter::StarterController& starterController)
    : flash(flash),
      apps1(hub.apps1()),
      apps2(hub.apps2()),
      tps1(hub.tps1()),
      tps2(hub.tps2()),
      ittr(hub.ittr()),
      target(hub.target()),
      gps(hub.gps()),
      clutch(hub.clutch()),
      motorController(motorController),
      plausibilityValidator(plausibilityValidator),
      launchController(launchController),
      autoShifter(autoShifter),
      starterController(starterController),
      hub(hub) {}

namespace {

RotationParams toRotationParams(const dc_RotationSensorConfig& c) {
    return {
        .teeth = c.teeth,
        .windowTeeth = c.window_teeth,
        .maxWindowUs = c.max_window_ms * 1000,
        .staleUs = c.stale_ms * 1000,
        .bothEdges = c.both_edges,
    };
}

}  // namespace

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

    if (config.has_launch)
        launchController.setConfig(config.launch);

    if (config.has_auto_shift)
        autoShifter.setConfig(config.auto_shift, config.sensor_calib.gps.type);

    if (config.has_starter)
        starterController.setConfig(config.starter);

    if (config.has_rotation) {
        const dc_RotationConfig& rc = config.rotation;
        hub.pulseEngine().setParams(toRotationParams(rc.engine));
        hub.pulseClutchRpm().setParams(toRotationParams(rc.clutch));
        hub.pulseWheelFL().setParams(toRotationParams(rc.wheel_front.sensor));
        hub.pulseWheelFR().setParams(toRotationParams(rc.wheel_front.sensor));
        hub.pulseWheelRL().setParams(toRotationParams(rc.wheel_rear.sensor));
        hub.pulseWheelRR().setParams(toRotationParams(rc.wheel_rear.sensor));
        hub.pulseWheelFL().setDiameterMm(rc.wheel_front.tire_diameter_mm);
        hub.pulseWheelFR().setDiameterMm(rc.wheel_front.tire_diameter_mm);
        hub.pulseWheelRL().setDiameterMm(rc.wheel_rear.tire_diameter_mm);
        hub.pulseWheelRR().setDiameterMm(rc.wheel_rear.tire_diameter_mm);
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
    if (src.has_starter)
        config.starter = src.starter;
    if (src.has_rotation)
        config.rotation = src.rotation;
    // 新トップレベルメッセージ (例: traction) はここに追加
}

void Configurator::loadConfigFromFlash() {
    loadDefault();
    const auto loaded = flash.readProto<dc_Config>(CONFIG_FILE_NAME, dc_Config_fields);
    if (!loaded)
        return;
    overlayConfig(*loaded);
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

void Configurator::setTransmissionType(dc_TransmissionType type) {
    config.sensor_calib.gps.type = type;
    config.sensor_calib.has_gps = true;
    // ギアテーブルと auto-shifter (maxGear / パルス幅ロジック) を新しい type で再適用。
    // ist/normal の raw 値は別フィールドで保持されるため切替で失われない。
    calibrate();
    configChanged = true;
}

bool Configurator::importConfig(const dc_Config& cfg) {
    // 部分 Config が渡されたときに既存設定をゼロで上書きしないよう has_* チェック経由で反映
    overlayConfig(cfg);
    calibrate();
    configChanged = true;
    return save();
}

bool Configurator::save() {
    if (!configChanged)
        return true;
    // 書き込みの間 (約 150 ms) は ETC を止める (Flash が tick と受け渡す、§6.8)。失敗したら未保存のまま残す
    if (!flash.writeProto(CONFIG_FILE_NAME, dc_Config_fields, &config))
        return false;
    DebugLogger::log("save: normalMax=%g restrictedMax=%g",
                               (double)config.sensor_calib.target_tp_normal_max,
                               (double)config.sensor_calib.target_tp_restricted_max);
    configChanged = false;
    return true;
}

void Configurator::revert() {
    calibrateFromFlash();
    configChanged = false;
}

bool Configurator::formatFs() {
    // 現在の config は RAM(this->config) に保持済み。FS を消去してから無条件で書き戻す。
    // save() は configChanged を見るためここでは使わず直接書き込む (フォーマットで消えるため)。
    // 消去と書き戻しを 1 回の ETC 停止でまとめる (間で ETC が再開しないように)
    if (!flash.beginWrite())
        return false;
    bool ok = flash.format() && flash.writeProto(CONFIG_FILE_NAME, dc_Config_fields, &config);
    flash.endWrite();
    if (ok)
        configChanged = false;
    return ok;
}
