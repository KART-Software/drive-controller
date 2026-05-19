#include <Arduino.h>

#include "can_controller.hpp"
#include "commands/command_controller.hpp"
#include "commands/command_router.hpp"
#include "configurator.hpp"
#include "constants.hpp"
#include "etc/motor_controller.hpp"
#include "etc/plausibility_validator.hpp"
#include "launch/launch_controller.hpp"
#include "sensor/sensor_hub.hpp"
#include "serial/serial_debug_writer.hpp"
#include "serial/serial_protocol.hpp"
#include "util/log/debug_logger.hpp"

IntervalTimer motorControlTimer;
IntervalTimer sensorSamplingTimer;

SensorHub sensorHub;
CanController canController(sensorHub);

etc::PlausibilityValidator plausibilityValidator(sensorHub.apps1(),
                                                 sensorHub.apps2(),
                                                 sensorHub.ittr(),
                                                 sensorHub.tps1(),
                                                 sensorHub.tps2(),
                                                 sensorHub.target(),
                                                 sensorHub.bps());
etc::MotorController motorController(sensorHub.target(), sensorHub.tps1());
launch::LaunchController launchController(sensorHub);
Configurator configurator(sensorHub, motorController, plausibilityValidator, launchController);
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

    static SerialDebugWriter serialDebugWriter;
    DebugLogger::addWriter(&serialDebugWriter);

    pinMode(FUEL_PUMP_PIN, OUTPUT);
    digitalWrite(FUEL_PUMP_PIN, HIGH);
    sensorHub.begin();

    canController.begin();
    configurator.initialize();
    configurator.calibrateFromFlash();

    // Default mode until CAN mode-select frame is received
    sensorHub.mut.target().setModeNormal();
    motorController.initialize();
    motorController.setMotorOn();
    motorControlTimer.begin(motorControlISR, MOTOR_CONTROLL_CYCLE_TIME * 1000);  // ms -> us
    motorTimerRunning = true;

    // Start 8kHz sensor sampling ISR
    sensorSamplingTimer.begin(sensorSamplingISR, SENSOR_SAMPLING_RATE_US);

    // NVIC priority: motor ISR (highest=0) > sensor ISR (lower=16)
    motorControlTimer.priority(0);
    sensorSamplingTimer.priority(16);

    plausibilityValidator.initialize();

    commandController.registerCommands(commandRouter);
}

unsigned long lastLogTime = 0;
unsigned long lastPulseUpdateTime = 0;
unsigned long lastCanTime = 0;

void loop() {
    unsigned long now = millis();

    // Poll CAN for mode-select frame
    canController.poll();
    switch (canController.rxData().etcMode) {
        case CanEtcMode::CALIB:
            sensorHub.mut.target().setModeCalibration();
            break;
        case CanEtcMode::NORMAL:
            sensorHub.mut.target().setModeNormal();
            break;
        case CanEtcMode::RESTRICTED:
            sensorHub.mut.target().setModeRestricted();
            break;
        case CanEtcMode::MOTOR_OFF:
            sensorHub.mut.target().setModeMotorOff();
            if (motorController.isOn()) {
                motorController.setMotorOff();
                if (motorTimerRunning) {
                    motorControlTimer.end();
                    motorTimerRunning = false;
                }
            }
            break;
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

    // CAN TX — 60Hz
    if (now - lastCanTime >= CAN_TX_INTERVAL_MS) {
        lastCanTime = now;
        canController.send();
        launchController.update(canController.rxData().launchActive);
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
