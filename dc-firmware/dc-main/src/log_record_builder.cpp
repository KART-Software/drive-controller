#include "log_record_builder.hpp"

LogRecord buildLogRecord(uint64_t tUs, uint32_t nowMs, const LogSources& src, const LoopStats::Snapshot& sys) {
    const auto& [hub, plausibility, control, shifter, motor, experiment, starter, canBus] = src;
    const Adc& adc = hub.adc();
    const Imu::Vec3 accel = hub.imu().accel();
    const Imu::Vec3 gyro = hub.imu().gyro();
    uint16_t f = 0;
    if (hub.target().isManual())
        f |= LOG_FLAG_MANUAL;
    if (hub.target().isIttr())
        f |= LOG_FLAG_ITTR;
    if (hub.shutdownSig().isOn())
        f |= LOG_FLAG_SHUTDOWN_SIG;
    if (plausibility.currentlyValid())
        f |= LOG_FLAG_VALID;
    if (control.autoShiftOn(nowMs))
        f |= LOG_FLAG_AUTOSHIFT;
    if (control.launchOn(nowMs))  // launch は凍結 (消費側なし)
        f |= LOG_FLAG_LAUNCH;
    if (starter.relayOn())
        f |= LOG_FLAG_STARTER_RELAY;
    if (control.starterPressed(nowMs))
        f |= LOG_FLAG_STARTER_SWITCH;

    const CanState can = canBus.busState();  // ESR1 / ECR のレジスタ読み (組の揃いは問わない)

    const LogRecord rec = {
        .t_us = tUs,
        .adc = {adc.value[0], adc.value[1], adc.value[2], adc.value[3],
                adc.value[4], adc.value[5], adc.value[6], adc.value[7]},
        .apps1 = (float)hub.apps1().convertedValue(),
        .apps2 = (float)hub.apps2().convertedValue(),
        .ittr = (float)hub.ittr().convertedValue(),
        .tps1 = (float)hub.tps1().convertedValue(),
        .tps2 = (float)hub.tps2().convertedValue(),
        .bps = (float)hub.bps().convertedValue(),
        .clutch = (float)hub.clutch().convertedValue(),
        .accel = {accel[0], accel[1], accel[2]},
        .gyro = {gyro[0], gyro[1], gyro[2]},
        .wheel_hz = {hub.pulseWheelFL().hz(), hub.pulseWheelFR().hz(),
                     hub.pulseWheelRL().hz(), hub.pulseWheelRR().hz()},
        .engine_hz = hub.pulseEngine().hz(),
        .clutch_hz = hub.pulseClutchRpm().hz(),
        .wheel_count = {hub.pulseWheelFL().count(), hub.pulseWheelFR().count(),
                        hub.pulseWheelRL().count(), hub.pulseWheelRR().count()},
        .target_tp = (float)hub.target().getTarget(),
        .duty = motor.lastOutput(),
        .errors = plausibility.errorBits(),
        .flags = f,
        .gear = hub.gps().getGear(),
        .mode = (uint8_t)hub.target().getMode(),
        .autoshift_state = (uint8_t)shifter.state(),
        .exp_type = experiment.logType(),
        .exp_index = experiment.logIndex(),
        .exp_phase = experiment.logPhase(),
        .loop_max_us = sys.loopMaxUs,
        .sd_max_us = sys.sdMaxUs,
        .tick_max_us = (uint16_t)min(sys.tickMaxUs, (uint32_t)65535),
        .log_drops = (uint16_t)min(sys.logDrops, (uint32_t)65535),  // 飽和 (折り返すと「減った」ように見える)
        .can_fault = (uint8_t)can.fault,
        .can_tec = can.txErrors,
        .can_rec = can.rxErrors,
        .reserved = 0,
        .wheel_kmh = {hub.pulseWheelFL().kmh(), hub.pulseWheelFR().kmh(),
                      hub.pulseWheelRL().kmh(), hub.pulseWheelRR().kmh()},
        .engine_rpm = hub.pulseEngine().rpm(),
        .clutch_rpm = hub.pulseClutchRpm().rpm(),
    };
    return rec;
}
