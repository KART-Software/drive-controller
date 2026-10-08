#include "log_record_builder.hpp"

LogRecord buildLogRecord(uint32_t t_ms,
                         const SensorHub& hub,
                         const etc::PlausibilityValidator& plausibility,
                         const CanController& can,
                         const shift::AutoShifter& shifter,
                         const etc::MotorController& motor,
                         const etc::ExperimentRunner& experiment,
                         const LoopStats::Snapshot& sys) {
    static const float kZero3[3] = {0.0f, 0.0f, 0.0f};
    const Adc& adc = hub.adc();
    const Imu* imu = hub.imu();
    const float* accel = imu ? imu->accel : kZero3;
    const float* gyro = imu ? imu->gyro : kZero3;
    uint16_t f = 0;
    if (hub.target().isManual())
        f |= LOG_FLAG_MANUAL;
    if (hub.target().isIttr())
        f |= LOG_FLAG_ITTR;
    if (hub.shutdownSig().isOn())
        f |= LOG_FLAG_SHUTDOWN_SIG;
    if (plausibility.currentlyValid())
        f |= LOG_FLAG_VALID;
#if defined(CONTROL_INPUT_VIA_CAN)
    if (can.rxData().autoShiftActive)
#else
    if (hub.autoShiftSwitch().isOn())  // GPIO 直入力 (CAN 制御入力は凍結中)
#endif
        f |= LOG_FLAG_AUTOSHIFT;
    if (can.rxData().launchActive)  // launch は凍結 (常に false)
        f |= LOG_FLAG_LAUNCH;

    const LogRecord rec = {
        .t_ms = t_ms,
        .adc = {adc.value[0], adc.value[1], adc.value[2], adc.value[3],
                adc.value[4], adc.value[5], adc.value[6], adc.value[7]},
        .apps1 = (float)hub.apps1().convertedValue(),
        .apps2 = (float)hub.apps2().convertedValue(),
        .ittr = (float)hub.ittr().convertedValue(),
        .tps1 = (float)hub.tps1().convertedValue(),
        .tps2 = (float)hub.tps2().convertedValue(),
        .bps = (float)hub.bps().convertedValue(),
        .accel = {accel[0], accel[1], accel[2]},
        .gyro = {gyro[0], gyro[1], gyro[2]},
        .wheel = {hub.pulseWheelFL().getFrequencyHz(), hub.pulseWheelFR().getFrequencyHz(),
                  hub.pulseWheelRL().getFrequencyHz(), hub.pulseWheelRR().getFrequencyHz()},
        .rpm = hub.pulseEngine().getFrequencyHz(),
        .clutch_rpm = hub.pulseClutchRpm().getFrequencyHz(),
        .target_tp = (float)hub.target().getTarget(),
        .clutch = (float)hub.clutch().convertedValue(),
        .wheel_count = {hub.pulseWheelFL().count(), hub.pulseWheelFR().count(),
                        hub.pulseWheelRL().count(), hub.pulseWheelRR().count()},
        .errors = plausibility.errorBits(),
        .flags = f,
        .gear = hub.gps().getGear(),
        .mode = (uint8_t)hub.target().getMode(),
        .autoshift_state = (uint8_t)shifter.state(),
        .exp_type = experiment.logType(),
        .exp_index = experiment.logIndex(),
        .exp_phase = experiment.logPhase(),
        .duty = motor.lastOutput(),  // 停止中は 0 (setMotorOff でクリア = 常に実印加値)
        .vbat = 0.0f,                // CAN 受信予定 (MoTeC 0x5F1, 別 PR)
        .loop_max_us = sys.loopMaxUs,
        .sd_max_us = sys.sdMaxUs,
        .safety_max_us = (uint16_t)min(sys.safetyMaxUs, (uint32_t)65535),
        .log_drops = (uint16_t)min(sys.logDrops, (uint32_t)65535),  // 飽和 (折り返すと「減った」ように見える)
    };
    return rec;
}
