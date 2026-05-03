#include <Arduino.h>

#include "can_bus.hpp"
#include "commands/command_controller.hpp"
#include "commands/command_router.hpp"
#include "configurator.hpp"
#include "constants.hpp"
#include "etc/motor_controller.hpp"
#include "etc/plausibility_validator.hpp"
#include "sensor/sensor_hub.hpp"
#include "serial/serial_protocol.hpp"

IntervalTimer motorControlTimer;
IntervalTimer sensorSamplingTimer;

CanBus canBus;
SensorHub sensorHub;

etc::PlausibilityValidator plausibilityValidator(sensorHub.apps1(),
                                                 sensorHub.apps2(),
                                                 sensorHub.ittr(),
                                                 sensorHub.tps1(),
                                                 sensorHub.tps2(),
                                                 sensorHub.target(),
                                                 sensorHub.bps());
etc::MotorController motorController(sensorHub.target(), sensorHub.tps1());
Configurator configurator(sensorHub, motorController, plausibilityValidator);
CommandRouter commandRouter;
CommandController commandController(configurator, motorController, sensorHub.mut.target());

volatile bool motorTimerRunning = false;

void motorControlISR() {
    motorController.cycle();
}

void sensorSamplingISR() {
    sensorHub.read();
}

void setup() {
    SerialProtocol::initialize();

    pinMode(FUEL_PUMP_PIN, OUTPUT);
    digitalWrite(FUEL_PUMP_PIN, HIGH);
    sensorHub.begin();

    canBus.begin();
    configurator.initialize();
    configurator.calibrateFromFlash();

    // Default mode until CAN mode-select frame is received
    sensorHub.mut.target().setModeNormal();
    bool motorOnAllowed = true;
    motorController.initialize();
    if (motorOnAllowed) {
        motorController.setMotorOn();
        motorControlTimer.begin(motorControlISR, MOTOR_CONTROLL_CYCLE_TIME * 1000);  // ms -> us
        motorTimerRunning = true;
    }

    // Start 8kHz sensor sampling ISR
    sensorSamplingTimer.begin(sensorSamplingISR, SENSOR_SAMPLING_RATE_US);

    // NVIC priority: motor ISR (highest=0) > sensor ISR (lower=16)
    if (motorTimerRunning) {
        motorControlTimer.priority(0);
    }
    sensorSamplingTimer.priority(16);

    plausibilityValidator.initialize();

    commandController.registerCommands(commandRouter);
}

unsigned long lastLogTime = 0;
unsigned long lastPulseUpdateTime = 0;
unsigned long lastCanFastTime = 0;
unsigned long lastCanSlowTime = 0;

void loop() {
    unsigned long now = millis();

    // Poll CAN for mode-select frame
    uint8_t canMode;
    if (canBus.poll(canMode)) {
        switch (canMode) {
            case 0:
                sensorHub.mut.target().setModeCalibration();
                break;
            case 1:
                sensorHub.mut.target().setModeNormal();
                break;
            case 2:
                sensorHub.mut.target().setModeRestricted();
                break;
            default:
                break;
        }
    }

    // Update pulse counters periodically
    if (now - lastPulseUpdateTime >= PULSE_UPDATE_INTERVAL_MS) {
        lastPulseUpdateTime = now;
        sensorHub.updatePulse();
    }

    if (!plausibilityValidator.isCurrentlyValid()) {
        if (motorController.isOn()) {
            motorController.setMotorOff();
            if (motorTimerRunning) {
                motorControlTimer.end();
                motorTimerRunning = false;
            }
            digitalWrite(FUEL_PUMP_PIN, LOW);
        }
    }

    // CAN TX — fast frames (throttle, status) every 20ms
    if (now - lastCanFastTime >= CAN_TX_FAST_INTERVAL_MS) {
        lastCanFastTime = now;
        canBus.sendThrottleFrame(sensorHub.apps1().convertedValue(), sensorHub.apps2().convertedValue(),
                                 sensorHub.tps1().convertedValue(), sensorHub.tps2().convertedValue());
        canBus.sendStatusFrame(sensorHub.target().getTarget(), sensorHub.bps().convertedValue(),
                               plausibilityValidator.isValid() ? 0x0000 : 0x0001,
                               canBus.modeStringToId(sensorHub.target().getModeString()));
    }

    // CAN TX — slow frames (wheel speed, engine/IMU, gyro) every 100ms
    if (now - lastCanSlowTime >= CAN_TX_SLOW_INTERVAL_MS) {
        lastCanSlowTime = now;
        canBus.sendWheelSpeedFrame(sensorHub.wheelSpeedFL(), sensorHub.wheelSpeedFR(), sensorHub.wheelSpeedRL(),
                                   sensorHub.wheelSpeedRR());
        if (sensorHub.imu() != nullptr) {
            canBus.sendEngineImuFrame(sensorHub.engineRpm(), sensorHub.imu()->accel[0], sensorHub.imu()->accel[1],
                                      sensorHub.imu()->accel[2]);
            canBus.sendGyroFrame(sensorHub.imu()->gyro[0], sensorHub.imu()->gyro[1], sensorHub.imu()->gyro[2]);
        } else {
            canBus.sendEngineImuFrame(sensorHub.engineRpm(), 0, 0, 0);
        }
    }

    // Send sensor data via serial protocol (50Hz)
    if (now - lastLogTime >= SENSOR_SEND_INTERVAL) {
        lastLogTime = now;
        SerialProtocol::sendSensorData(sensorHub, plausibilityValidator.isValid(),
                                       plausibilityValidator.getErrorHandler());
    }

    // Command polling
    commandRouter.poll();
}
