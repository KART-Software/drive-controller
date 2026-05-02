#include "command_controller.hpp"
#include "serial/serial_protocol.hpp"

namespace {

void sendConfigResponse(CommandContainer* c, uint32_t id, bool ok) {
    dc_Config cfg;
    c->configurator.getConfigProto(cfg);
    SerialProtocol::sendResponseWithConfig(id, ok, cfg);
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
    p.apps1_min = c->configurator.config.sensorValues.apps1Min;
    p.apps2_min = c->configurator.config.sensorValues.apps2Min;
    p.ittr_min = c->configurator.config.sensorValues.ittrMin;
    SerialProtocol::sendResponseWithAppsMin(cmd.id, true, p);
}

void setAppsMax(void* ctx, const dc_Command& cmd) {
    auto* c = static_cast<CommandContainer*>(ctx);
    c->configurator.setAppsMax();
    dc_AppsMaxResponse p = dc_AppsMaxResponse_init_zero;
    p.apps1_max = c->configurator.config.sensorValues.apps1Max;
    p.apps2_max = c->configurator.config.sensorValues.apps2Max;
    p.ittr_max = c->configurator.config.sensorValues.ittrMax;
    SerialProtocol::sendResponseWithAppsMax(cmd.id, true, p);
}

void setTpsMin(void* ctx, const dc_Command& cmd) {
    auto* c = static_cast<CommandContainer*>(ctx);
    c->configurator.setTpsMin();
    dc_TpsMinResponse p = dc_TpsMinResponse_init_zero;
    p.tps1_min = c->configurator.config.sensorValues.tps1Min;
    p.tps2_min = c->configurator.config.sensorValues.tps2Min;
    SerialProtocol::sendResponseWithTpsMin(cmd.id, true, p);
}

void setTpsMax(void* ctx, const dc_Command& cmd) {
    auto* c = static_cast<CommandContainer*>(ctx);
    c->configurator.setTpsMax();
    dc_TpsMaxResponse p = dc_TpsMaxResponse_init_zero;
    p.tps1_max = c->configurator.config.sensorValues.tps1Max;
    p.tps2_max = c->configurator.config.sensorValues.tps2Max;
    SerialProtocol::sendResponseWithTpsMax(cmd.id, true, p);
}

void setIdling(void* ctx, const dc_Command& cmd) {
    auto* c = static_cast<CommandContainer*>(ctx);
    c->configurator.setIdling();
    dc_IdlingResponse p = dc_IdlingResponse_init_zero;
    p.idling = c->configurator.config.sensorValues.idling;
    SerialProtocol::sendResponseWithIdling(cmd.id, true, p);
}

void setEtcTargetBound(void* ctx, const dc_Command& cmd) {
    auto* c = static_cast<CommandContainer*>(ctx);
    const dc_SetEtcTargetBoundCmd& d = cmd.body.set_etc_target_bound;
    double idling = d.has_idling ? d.idling : c->configurator.config.sensorValues.idling;
    double normalMax = d.has_normal_max ? d.normal_max : c->configurator.config.sensorValues.normalMax;
    double restrictedMax = d.has_restricted_max ? d.restricted_max : c->configurator.config.sensorValues.restrictedMax;
    c->configurator.setTargetBound(idling, normalMax, restrictedMax);
    dc_TargetBoundResponse p = dc_TargetBoundResponse_init_zero;
    p.idling = c->configurator.config.sensorValues.idling;
    p.normal_max = c->configurator.config.sensorValues.normalMax;
    p.restricted_max = c->configurator.config.sensorValues.restrictedMax;
    SerialProtocol::sendResponseWithTargetBound(cmd.id, true, p);
}

void setPlausibilityCheckFlags(void* ctx, const dc_Command& cmd) {
    auto* c = static_cast<CommandContainer*>(ctx);
    const dc_SetEtcPlausibilityFlagsCmd& d = cmd.body.set_plausibility_flags;
    PlausibilityCheckFlags flags;
    flags.apps = d.has_apps ? d.apps : false;
    flags.tps = d.has_tps ? d.tps : false;
    flags.apps1 = d.has_apps1 ? d.apps1 : false;
    flags.apps2 = d.has_apps2 ? d.apps2 : false;
    flags.tps1 = d.has_tps1 ? d.tps1 : false;
    flags.tps2 = d.has_tps2 ? d.tps2 : false;
    flags.target = d.has_target ? d.target : false;
    flags.bps = d.has_bps ? d.bps : false;
    flags.bpsTps = d.has_bps_tps ? d.bps_tps : false;
    c->configurator.setPlausibilityFlags(flags);
    dc_EtcPlausibilityCheckFlags p = dc_EtcPlausibilityCheckFlags_init_zero;
    p.apps = flags.apps;
    p.tps = flags.tps;
    p.apps1 = flags.apps1;
    p.apps2 = flags.apps2;
    p.tps1 = flags.tps1;
    p.tps2 = flags.tps2;
    p.target = flags.target;
    p.bps = flags.bps;
    p.bps_tps = flags.bpsTps;
    SerialProtocol::sendResponseWithFlags(cmd.id, true, p);
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
    double kP = d.has_k_p ? d.k_p : c->configurator.config.pid.kP;
    double kI = d.has_k_i ? d.k_i : c->configurator.config.pid.kI;
    double kD = d.has_k_d ? d.k_d : c->configurator.config.pid.kD;
    c->configurator.setPid(kP, kI, kD);
    dc_EtcPid p = dc_EtcPid_init_zero;
    p.k_p = kP;
    p.k_i = kI;
    p.k_d = kD;
    SerialProtocol::sendResponseWithPid(cmd.id, true, p);
}

void setEtcTargetCurve(void* ctx, const dc_Command& cmd) {
    auto* c = static_cast<CommandContainer*>(ctx);
    const dc_SetEtcTargetCurveCmd& d = cmd.body.set_etc_target_curve;
    TargetCurve curve;
    curve.a4 = d.has_a4 ? d.a4 : c->configurator.config.targetCurve.a4;
    curve.a3 = d.has_a3 ? d.a3 : c->configurator.config.targetCurve.a3;
    curve.a2 = d.has_a2 ? d.a2 : c->configurator.config.targetCurve.a2;
    curve.a1 = d.has_a1 ? d.a1 : c->configurator.config.targetCurve.a1;
    c->configurator.setTargetCurve(curve);
    dc_EtcTargetCurve p = dc_EtcTargetCurve_init_zero;
    p.a4 = curve.a4;
    p.a3 = curve.a3;
    p.a2 = curve.a2;
    p.a1 = curve.a1;
    SerialProtocol::sendResponseWithCurve(cmd.id, true, p);
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

}  // namespace

CommandController::CommandController(Configurator& configurator, MotorController& motorController, Target& target)
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
}
