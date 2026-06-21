#include "command_controller.hpp"
#include "serial/serial_protocol.hpp"

#include <TimeLib.h>

namespace {

void sendConfigResponse(CommandContainer* c, uint32_t id, bool ok) {
    dc_ConfigResponse cfgResp = dc_ConfigResponse_init_zero;
    cfgResp.has_config = true;
    cfgResp.config = c->configurator.config;
    cfgResp.changed = c->configurator.configChanged;
    cfgResp.fs_used = (uint32_t)c->configurator.flash.usedSize();
    cfgResp.fs_total = (uint32_t)c->configurator.flash.totalSize();
    SerialProtocol::sendResponseWithConfig(id, ok, cfgResp);
}

void etcMotorOff(void* ctx, const dc_Command& cmd) {
    static_cast<CommandContainer*>(ctx)->motorController.setMotorOff();
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
    dc_AppsMinResponse p = dc_AppsMinResponse_init_zero;
    p.apps1_min = c->configurator.config.sensor_calib.apps1_min;
    p.apps2_min = c->configurator.config.sensor_calib.apps2_min;
    p.ittr_min = c->configurator.config.sensor_calib.ittr_min;
    SerialProtocol::sendResponseWithAppsMin(cmd.id, true, p);
}

void setAppsMax(void* ctx, const dc_Command& cmd) {
    auto* c = static_cast<CommandContainer*>(ctx);
    c->configurator.setAppsMax();
    dc_AppsMaxResponse p = dc_AppsMaxResponse_init_zero;
    p.apps1_max = c->configurator.config.sensor_calib.apps1_max;
    p.apps2_max = c->configurator.config.sensor_calib.apps2_max;
    p.ittr_max = c->configurator.config.sensor_calib.ittr_max;
    SerialProtocol::sendResponseWithAppsMax(cmd.id, true, p);
}

void setTpsMin(void* ctx, const dc_Command& cmd) {
    auto* c = static_cast<CommandContainer*>(ctx);
    c->configurator.setTpsMin();
    dc_TpsMinResponse p = dc_TpsMinResponse_init_zero;
    p.tps1_min = c->configurator.config.sensor_calib.tps1_min;
    p.tps2_min = c->configurator.config.sensor_calib.tps2_min;
    SerialProtocol::sendResponseWithTpsMin(cmd.id, true, p);
}

void setTpsMax(void* ctx, const dc_Command& cmd) {
    auto* c = static_cast<CommandContainer*>(ctx);
    c->configurator.setTpsMax();
    dc_TpsMaxResponse p = dc_TpsMaxResponse_init_zero;
    p.tps1_max = c->configurator.config.sensor_calib.tps1_max;
    p.tps2_max = c->configurator.config.sensor_calib.tps2_max;
    SerialProtocol::sendResponseWithTpsMax(cmd.id, true, p);
}

void setIdling(void* ctx, const dc_Command& cmd) {
    auto* c = static_cast<CommandContainer*>(ctx);
    c->configurator.setIdling();
    dc_IdlingResponse p = dc_IdlingResponse_init_zero;
    p.idling = c->configurator.config.sensor_calib.target_tp_idling;
    SerialProtocol::sendResponseWithIdling(cmd.id, true, p);
}

void setEtcTargetBound(void* ctx, const dc_Command& cmd) {
    auto* c = static_cast<CommandContainer*>(ctx);
    const dc_SetEtcTargetBoundCmd& d = cmd.body.set_etc_target_bound;
    float idling = d.has_idling ? d.idling : c->configurator.config.sensor_calib.target_tp_idling;
    float normalMax = d.has_normal_max ? d.normal_max : c->configurator.config.sensor_calib.target_tp_normal_max;
    float restrictedMax =
        d.has_restricted_max ? d.restricted_max : c->configurator.config.sensor_calib.target_tp_restricted_max;
    c->configurator.setTargetBound(idling, normalMax, restrictedMax);
    dc_TargetBoundResponse p = dc_TargetBoundResponse_init_zero;
    p.idling = c->configurator.config.sensor_calib.target_tp_idling;
    p.normal_max = c->configurator.config.sensor_calib.target_tp_normal_max;
    p.restricted_max = c->configurator.config.sensor_calib.target_tp_restricted_max;
    SerialProtocol::sendResponseWithTargetBound(cmd.id, true, p);
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
    dc_IttrResponse p = dc_IttrResponse_init_zero;
    p.use = cmd.body.set_ittr.use;
    SerialProtocol::sendResponseWithIttr(cmd.id, true, p);
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
    bool ok = static_cast<CommandContainer*>(ctx)->target.setManual();
    SerialProtocol::sendResponse(cmd.id, ok);
}

void etcManualAdjust(void* ctx, const dc_Command& cmd) {
    static_cast<CommandContainer*>(ctx)->target.manualAdjust(cmd.body.etc_manual_adjust.amount);
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
    dc_GpsGearResponse p = dc_GpsGearResponse_init_zero;
    p.gear = gear;
    const dc_GpsCalib& gc = c->configurator.config.sensor_calib.gps;
    if (gc.type == dc_TransmissionType_TRANSMISSION_IST) {
        const int8_t gearsArr[] = GPS_IST_GEARS;
        p.raw_values_count = gc.ist_raw_values_count;
        p.gears_count = gc.ist_raw_values_count;
        for (uint8_t i = 0; i < gc.ist_raw_values_count && i < GPS_IST_GEAR_COUNT; i++) {
            p.raw_values[i] = gc.ist_raw_values[i];
            p.gears[i] = gearsArr[i];
        }
    } else {
        const int8_t gearsArr[] = GPS_NORMAL_GEARS;
        p.raw_values_count = gc.normal_raw_values_count;
        p.gears_count = gc.normal_raw_values_count;
        for (uint8_t i = 0; i < gc.normal_raw_values_count && i < GPS_NORMAL_GEAR_COUNT; i++) {
            p.raw_values[i] = gc.normal_raw_values[i];
            p.gears[i] = gearsArr[i];
        }
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

}  // namespace

CommandController::CommandController(Configurator& configurator,
                                     etc::MotorController& motorController,
                                     EtcTarget& target)
    : container{configurator, motorController, target} {}

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
}
