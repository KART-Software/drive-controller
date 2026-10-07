#include "command_controller.hpp"
#include "serial/serial_protocol.hpp"

#include <TimeLib.h>

namespace {

void sendConfigResponse(CommandContainer* c, uint32_t id, bool ok) {
    SerialProtocol::sendResponseWithConfig(id, ok,
                                           {
                                               .has_config = true,
                                               .config = c->configurator.config,
                                               .changed = c->configurator.configChanged,
                                               .fs_used = (uint32_t)c->configurator.flash.usedSize(),
                                               .fs_total = (uint32_t)c->configurator.flash.totalSize(),
                                           });
}

void etcMotorOff(void* ctx, const dc_Command& cmd) {
    auto* c = static_cast<CommandContainer*>(ctx);
    if (c->experimentRunner.active()) {  // 実験中は拒否 (spec Q17)
        SerialProtocol::sendResponse(cmd.id, false);
        return;
    }
    c->motorController.setMotorOff();
    SerialProtocol::sendResponse(cmd.id, true);
}

void save(void* ctx, const dc_Command& cmd) {
    auto* c = static_cast<CommandContainer*>(ctx);
    c->configurator.save();
    sendConfigResponse(c, cmd.id, true);
}

void setAppsMin(void* ctx, const dc_Command& cmd) {
    auto* c = static_cast<CommandContainer*>(ctx);
    c->configurator.setAppsMin();
    SerialProtocol::sendResponseWithAppsMin(cmd.id, true, {.apps1_min = c->configurator.config.sensor_calib.apps1_min, .apps2_min = c->configurator.config.sensor_calib.apps2_min, .ittr_min = c->configurator.config.sensor_calib.ittr_min});
}

void setAppsMax(void* ctx, const dc_Command& cmd) {
    auto* c = static_cast<CommandContainer*>(ctx);
    c->configurator.setAppsMax();
    SerialProtocol::sendResponseWithAppsMax(cmd.id, true, {.apps1_max = c->configurator.config.sensor_calib.apps1_max, .apps2_max = c->configurator.config.sensor_calib.apps2_max, .ittr_max = c->configurator.config.sensor_calib.ittr_max});
}

void setTpsMin(void* ctx, const dc_Command& cmd) {
    auto* c = static_cast<CommandContainer*>(ctx);
    c->configurator.setTpsMin();
    SerialProtocol::sendResponseWithTpsMin(cmd.id, true, {.tps1_min = c->configurator.config.sensor_calib.tps1_min, .tps2_min = c->configurator.config.sensor_calib.tps2_min});
}

void setTpsMax(void* ctx, const dc_Command& cmd) {
    auto* c = static_cast<CommandContainer*>(ctx);
    c->configurator.setTpsMax();
    SerialProtocol::sendResponseWithTpsMax(cmd.id, true, {.tps1_max = c->configurator.config.sensor_calib.tps1_max, .tps2_max = c->configurator.config.sensor_calib.tps2_max});
}

void setIdling(void* ctx, const dc_Command& cmd) {
    auto* c = static_cast<CommandContainer*>(ctx);
    c->configurator.setIdling();
    SerialProtocol::sendResponseWithIdling(cmd.id, true, {.idling = c->configurator.config.sensor_calib.target_tp_idling});
}

void setEtcTargetBound(void* ctx, const dc_Command& cmd) {
    auto* c = static_cast<CommandContainer*>(ctx);
    const dc_SetEtcTargetBoundCmd& d = cmd.body.set_etc_target_bound;
    float idling = d.has_idling ? d.idling : c->configurator.config.sensor_calib.target_tp_idling;
    float normalMax = d.has_normal_max ? d.normal_max : c->configurator.config.sensor_calib.target_tp_normal_max;
    float restrictedMax =
        d.has_restricted_max ? d.restricted_max : c->configurator.config.sensor_calib.target_tp_restricted_max;
    c->configurator.setTargetBound(idling, normalMax, restrictedMax);
    SerialProtocol::sendResponseWithTargetBound(cmd.id, true, {.idling = c->configurator.config.sensor_calib.target_tp_idling, .normal_max = c->configurator.config.sensor_calib.target_tp_normal_max, .restricted_max = c->configurator.config.sensor_calib.target_tp_restricted_max});
}

void setPlausibilityCheckFlags(void* ctx, const dc_Command& cmd) {
    auto* c = static_cast<CommandContainer*>(ctx);
    const dc_SetEtcPlausibilityFlagsCmd& d = cmd.body.set_plausibility_flags;
    dc_EtcPlausibilityCheckFlags flags = c->configurator.config.etc.plausibility_check_flags;
    if (d.has_apps)
        flags.apps = d.apps;
    if (d.has_tps)
        flags.tps = d.tps;
    if (d.has_apps1)
        flags.apps1 = d.apps1;
    if (d.has_apps2)
        flags.apps2 = d.apps2;
    if (d.has_tps1)
        flags.tps1 = d.tps1;
    if (d.has_tps2)
        flags.tps2 = d.tps2;
    if (d.has_target)
        flags.target = d.target;
    if (d.has_bps)
        flags.bps = d.bps;
    if (d.has_bps_tps)
        flags.bps_tps = d.bps_tps;
    c->configurator.setPlausibilityFlags(flags);
    SerialProtocol::sendResponseWithFlags(cmd.id, true, flags);
}

void setIttr(void* ctx, const dc_Command& cmd) {
    auto* c = static_cast<CommandContainer*>(ctx);
    c->configurator.setIttrFlag(cmd.body.set_ittr.use);
    SerialProtocol::sendResponseWithIttr(cmd.id, true, {.use = cmd.body.set_ittr.use});
}

void setEtcPid(void* ctx, const dc_Command& cmd) {
    auto* c = static_cast<CommandContainer*>(ctx);
    const dc_SetEtcPidCmd& d = cmd.body.set_etc_pid;
    dc_EtcPid pid = c->configurator.config.etc.pid;
    if (d.has_k_p)
        pid.k_p = d.k_p;
    if (d.has_k_i)
        pid.k_i = d.k_i;
    if (d.has_k_d)
        pid.k_d = d.k_d;
    c->configurator.setPid(pid.k_p, pid.k_i, pid.k_d);
    SerialProtocol::sendResponseWithPid(cmd.id, true, pid);
}

void setEtcTargetCurve(void* ctx, const dc_Command& cmd) {
    auto* c = static_cast<CommandContainer*>(ctx);
    const dc_SetEtcTargetCurveCmd& d = cmd.body.set_etc_target_curve;
    dc_EtcTargetCurve curve = c->configurator.config.etc.target_curve;
    if (d.has_a4)
        curve.a4 = d.a4;
    if (d.has_a3)
        curve.a3 = d.a3;
    if (d.has_a2)
        curve.a2 = d.a2;
    if (d.has_a1)
        curve.a1 = d.a1;
    c->configurator.setTargetCurve(curve);
    SerialProtocol::sendResponseWithCurve(cmd.id, true, curve);
}

void setEtcManual(void* ctx, const dc_Command& cmd) {
    auto* c = static_cast<CommandContainer*>(ctx);
    if (c->experimentRunner.active()) {  // manual target を奪い合うため実験中は拒否 (spec Q17)
        SerialProtocol::sendResponse(cmd.id, false);
        return;
    }
    SerialProtocol::sendResponse(cmd.id, c->target.setManual());
}

void etcManualAdjust(void* ctx, const dc_Command& cmd) {
    auto* c = static_cast<CommandContainer*>(ctx);
    if (c->experimentRunner.active()) {  // 同上 (spec Q17)
        SerialProtocol::sendResponse(cmd.id, false);
        return;
    }
    c->target.manualAdjust(cmd.body.etc_manual_adjust.amount);
    SerialProtocol::sendResponse(cmd.id, true);
}

void getConfig(void* ctx, const dc_Command& cmd) {
    sendConfigResponse(static_cast<CommandContainer*>(ctx), cmd.id, true);
}

void setConfig(void* ctx, const dc_Command& cmd) {
    auto* c = static_cast<CommandContainer*>(ctx);
    bool ok = false;
    if (cmd.body.set_config.has_config) {
        ok = c->configurator.importConfig(cmd.body.set_config.config);
    }
    SerialProtocol::sendResponse(cmd.id, ok);
}

void reboot(void* ctx, const dc_Command& cmd) {
    (void)ctx;
    SerialProtocol::sendResponse(cmd.id, true);
    delay(100);
    SCB_AIRCR = 0x05FA0004;  // Teensy software reset
}

void revert(void* ctx, const dc_Command& cmd) {
    auto* c = static_cast<CommandContainer*>(ctx);
    c->configurator.revert();
    sendConfigResponse(c, cmd.id, true);
}

void setRtc(void* ctx, const dc_Command& cmd) {
    (void)ctx;
    uint32_t epoch = cmd.body.set_rtc.epoch;
    Teensy3Clock.set(epoch);
    setTime(epoch);
    SerialProtocol::sendResponse(cmd.id, true);
}

void setGpsGear(void* ctx, const dc_Command& cmd) {
    auto* c = static_cast<CommandContainer*>(ctx);
    int8_t gear = (int8_t)cmd.body.set_gps_gear.gear;
    c->configurator.setGpsGear(gear);
    const dc_GpsCalib& gc = c->configurator.config.sensor_calib.gps;
    const bool ist = gc.type == dc_TransmissionType_TRANSMISSION_IST;
    static const int8_t kIstGears[] = GPS_IST_GEARS;
    static const int8_t kNormalGears[] = GPS_NORMAL_GEARS;
    const int8_t* gears = ist ? kIstGears : kNormalGears;
    const uint32_t* raws = ist ? gc.ist_raw_values : gc.normal_raw_values;
    const pb_size_t stored = ist ? gc.ist_raw_values_count : gc.normal_raw_values_count;
    const pb_size_t cap = ist ? GPS_IST_GEAR_COUNT : GPS_NORMAL_GEAR_COUNT;
    const pb_size_t n = stored < cap ? stored : cap;
    dc_GpsGearResponse p = {.gear = gear, .raw_values_count = n, .gears_count = n};
    for (pb_size_t i = 0; i < n; i++) {  // 配列メンバは指定子初期化できないので代入
        p.raw_values[i] = raws[i];
        p.gears[i] = gears[i];
    }
    SerialProtocol::sendResponseWithGpsGear(cmd.id, true, p);
}

void setClutchMin(void* ctx, const dc_Command& cmd) {
    auto* c = static_cast<CommandContainer*>(ctx);
    c->configurator.setClutchMin();
    sendConfigResponse(c, cmd.id, true);
}

void setClutchMax(void* ctx, const dc_Command& cmd) {
    auto* c = static_cast<CommandContainer*>(ctx);
    c->configurator.setClutchMax();
    sendConfigResponse(c, cmd.id, true);
}

void setTransmissionType(void* ctx, const dc_Command& cmd) {
    auto* c = static_cast<CommandContainer*>(ctx);
    c->configurator.setTransmissionType(cmd.body.set_transmission_type.type);
    sendConfigResponse(c, cmd.id, true);
}

void formatFs(void* ctx, const dc_Command& cmd) {
    auto* c = static_cast<CommandContainer*>(ctx);
    bool ok = c->configurator.formatFs();  // FS 消去 → 現在の config を書き戻す
    sendConfigResponse(c, cmd.id, ok);     // 更新後の fs_used/fs_total を返す
}

void startEtcExperiment(void* ctx, const dc_Command& cmd) {
    auto* c = static_cast<CommandContainer*>(ctx);
    etc::ExperimentRunner::Type t;
    switch (cmd.body.start_etc_experiment.type) {
        case dc_StartEtcExperimentCmd_Type_POINT_DWELL:
            t = etc::ExperimentRunner::Type::PointDwell;
            break;
        case dc_StartEtcExperimentCmd_Type_RELEASE:
            t = etc::ExperimentRunner::Type::Release;
            break;
        case dc_StartEtcExperimentCmd_Type_STEP:
            t = etc::ExperimentRunner::Type::Step;
            break;
        default:
            SerialProtocol::sendResponse(cmd.id, false);
            return;
    }
    // 前提条件 (spec §5: ノブ≠MOTOR_OFF・エンジン停止・車両静止・plausibility・SD) は
    // start() が検証し、不成立なら false 応答になる。
    SerialProtocol::sendResponse(cmd.id, c->experimentRunner.start(t, millis()));
}

void stopEtcExperiment(void* ctx, const dc_Command& cmd) {
    static_cast<CommandContainer*>(ctx)->experimentRunner.stop();
    SerialProtocol::sendResponse(cmd.id, true);
}

// 制御入力の上書き (ベンチ用)。0x740 受信中は ControlInput が拒否する。
void setControlOverride(void* ctx, const dc_Command& cmd) {
    auto* c = static_cast<CommandContainer*>(ctx);
    const dc_SetControlOverrideCmd& d = cmd.body.set_control_override;
    if (!d.enable) {
        c->controlInput.clearOverride();
        SerialProtocol::sendResponse(cmd.id, true);
        return;
    }
    const ControlInput::Override o = {
        .hasEtcMode = d.has_etc_mode,
        .etcMode = etcModeFromProto(d.etc_mode),
        .hasAutoShift = d.has_auto_shift,
        .autoShift = d.auto_shift,
    };
    SerialProtocol::sendResponse(cmd.id, c->controlInput.setOverride(o));
}

}  // namespace

CommandController::CommandController(Configurator& configurator,
                                     etc::MotorController& motorController,
                                     EtcTarget& target,
                                     etc::ExperimentRunner& experimentRunner,
                                     ControlInput& controlInput)
    : container{configurator, motorController, target, experimentRunner, controlInput} {}

void CommandController::registerCommands(CommandRouter& router) {
    router.on(dc_Command_etc_motor_off_tag, etcMotorOff, &container);
    router.on(dc_Command_save_tag, save, &container);
    router.on(dc_Command_set_apps_min_tag, setAppsMin, &container);
    router.on(dc_Command_set_apps_max_tag, setAppsMax, &container);
    router.on(dc_Command_set_tps_min_tag, setTpsMin, &container);
    router.on(dc_Command_set_tps_max_tag, setTpsMax, &container);
    router.on(dc_Command_set_idling_tag, setIdling, &container);
    router.on(dc_Command_set_etc_target_bound_tag, setEtcTargetBound, &container);
    router.on(dc_Command_set_plausibility_flags_tag, setPlausibilityCheckFlags, &container);
    router.on(dc_Command_set_ittr_tag, setIttr, &container);
    router.on(dc_Command_set_etc_pid_tag, setEtcPid, &container);
    router.on(dc_Command_set_etc_target_curve_tag, setEtcTargetCurve, &container);
    router.on(dc_Command_set_etc_manual_tag, setEtcManual, &container);
    router.on(dc_Command_etc_manual_adjust_tag, etcManualAdjust, &container);
    router.on(dc_Command_get_config_tag, getConfig, &container);
    router.on(dc_Command_set_config_tag, setConfig, &container);
    router.on(dc_Command_reboot_tag, reboot, nullptr);
    router.on(dc_Command_revert_tag, revert, &container);
    router.on(dc_Command_set_rtc_tag, setRtc, nullptr);
    router.on(dc_Command_set_gps_gear_tag, setGpsGear, &container);
    router.on(dc_Command_set_clutch_min_tag, setClutchMin, &container);
    router.on(dc_Command_set_clutch_max_tag, setClutchMax, &container);
    router.on(dc_Command_set_transmission_type_tag, setTransmissionType, &container);
    router.on(dc_Command_format_fs_tag, formatFs, &container);
    router.on(dc_Command_start_etc_experiment_tag, startEtcExperiment, &container);
    router.on(dc_Command_stop_etc_experiment_tag, stopEtcExperiment, &container);
    router.on(dc_Command_set_control_override_tag, setControlOverride, &container);
}
