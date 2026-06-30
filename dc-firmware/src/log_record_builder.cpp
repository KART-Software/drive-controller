#include "log_record_builder.hpp"

LogRecord buildLogRecord(uint32_t t_ms,
                         const SensorHub& hub,
                         const etc::PlausibilityValidator& plausibility,
                         const CanController& can,
                         const shift::AutoShifter& shifter) {
    LogRecord rec = {};
    rec.t_ms = t_ms;

    const Adc& adc = hub.adc();
    for (int i = 0; i < 8; i++)
        rec.adc[i] = adc.value[i];

    rec.apps1 = (float)hub.apps1().convertedValue();
    rec.apps2 = (float)hub.apps2().convertedValue();
    rec.ittr = (float)hub.ittr().convertedValue();
    rec.tps1 = (float)hub.tps1().convertedValue();
    rec.tps2 = (float)hub.tps2().convertedValue();
    rec.bps = (float)hub.bps().convertedValue();

    const Imu* imu = hub.imu();
    if (imu != nullptr) {
        for (int i = 0; i < 3; i++) {
            rec.accel[i] = imu->accel[i];
            rec.gyro[i] = imu->gyro[i];
        }
    }

    rec.wheel[0] = hub.pulseWheelFL().getFrequencyHz();
    rec.wheel[1] = hub.pulseWheelFR().getFrequencyHz();
    rec.wheel[2] = hub.pulseWheelRL().getFrequencyHz();
    rec.wheel[3] = hub.pulseWheelRR().getFrequencyHz();
    rec.rpm = hub.pulseEngine().getFrequencyHz();
    rec.clutch_rpm = hub.pulseClutchRpm().getFrequencyHz();
    rec.target_tp = (float)hub.target().getTarget();
    rec.clutch = (float)hub.clutch().convertedValue();

    rec.wheel_count[0] = hub.pulseWheelFL().count();
    rec.wheel_count[1] = hub.pulseWheelFR().count();
    rec.wheel_count[2] = hub.pulseWheelRL().count();
    rec.wheel_count[3] = hub.pulseWheelRR().count();

    rec.errors = plausibility.errorBits();
    rec.gear = hub.gps().getGear();
    rec.mode = (uint8_t)hub.target().getMode();
    rec.autoshift_state = (uint8_t)shifter.state();

    uint16_t f = 0;
    if (hub.target().isManual())
        f |= LOG_FLAG_MANUAL;
    if (hub.target().isIttr())
        f |= LOG_FLAG_ITTR;
    if (hub.shutdownSig().isOn())
        f |= LOG_FLAG_SHUTDOWN_SIG;
    if (plausibility.currentlyValid())
        f |= LOG_FLAG_VALID;
    if (can.rxData().autoShiftActive)
        f |= LOG_FLAG_AUTOSHIFT;
    if (can.rxData().launchActive)
        f |= LOG_FLAG_LAUNCH;
    rec.flags = f;

    return rec;
}
