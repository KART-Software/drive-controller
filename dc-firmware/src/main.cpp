#include <Arduino.h>

#include "can/can_controller.hpp"
#include "commands/command_controller.hpp"
#include "commands/command_router.hpp"
#include "configurator.hpp"
#include "constants.hpp"
#include "etc/motor_controller.hpp"
#include "etc/plausibility_validator.hpp"
#include "launch/launch_controller.hpp"
#include "sensor/sensor_hub.hpp"
#include "shift/auto_shifter.hpp"
#include "serial/serial_debug_writer.hpp"
#include "serial/serial_protocol.hpp"
#include "util/flash.hpp"
#include "util/log/debug_logger.hpp"

IntervalTimer motorControlTimer;
IntervalTimer sensorSamplingTimer;

Flash flash;
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
launch::LaunchController launchController(sensorHub.pulseEngine(),
                                          sensorHub.pulseWheelRL(),
                                          sensorHub.pulseWheelRR(),
                                          sensorHub.clutch(),
                                          flash);
shift::AutoShifter autoShifter(sensorHub.pulseEngine(),
                               sensorHub.pulseWheelFL(),
                               sensorHub.pulseWheelFR(),
                               sensorHub.gps(),
                               sensorHub.apps1());
Configurator configurator(flash, sensorHub, motorController, plausibilityValidator, launchController, autoShifter);
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
    flash.initialize();
    configurator.calibrateFromFlash();
#if defined(LAUNCH_CONTROL_ENABLED)
    launchController.begin();
#endif
    autoShifter.begin();

    // Default mode until CAN mode-select frame is received
    sensorHub.mut.target().setModeNormal();
    motorController.initialize();
    motorController.setMotorOn();
    motorControlTimer.begin(motorControlISR, MOTOR_CONTROLL_CYCLE_TIME * 1000);  // ms -> us
    motorTimerRunning = true;
    motorControlTimer.priority(0);  // motor ISR を最高優先

    // NOTE: 8kHz サンプリング ISR は ADC/IMU の SPI ブロッキングで USB(低優先割込)
    // を枯渇させ、列挙されてもシリアルポートが開けなくなる。known-good の etc と
    // 同様に sensorHub.read() は loop() (スレッドレベル) で呼ぶ。高レートサンプリングが
    // 必要なら非ブロッキング/DMA SPI 化してから ISR 化すること。
    // sensorSamplingTimer.begin(sensorSamplingISR, SENSOR_SAMPLING_RATE_US);
    // sensorSamplingTimer.priority(16);

    plausibilityValidator.initialize();

    commandController.registerCommands(commandRouter);
}

unsigned long lastLogTime = 0;
unsigned long lastPulseUpdateTime = 0;
unsigned long lastCanTime = 0;
unsigned long lastLaunchTime = 0;

void loop() {
    unsigned long now = millis();

    // センサー読み (ADC + sensor.update + IMU)。8kHz ISR ではなく loop で行う (上記 NOTE)。
    sensorHub.read();

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
    }

#if defined(LAUNCH_CONTROL_ENABLED)
    // Launch FSM tick — 20Hz (pulse counter 周期 100ms と整合)
    // Plausibility 違反 / MOTOR_OFF 時は launchRequested を強制 false にして
    // FSM を Idle に戻す (handler 側の !launchRequested パスで motor_.off() 経由)。
    if (now - lastLaunchTime >= LAUNCH_UPDATE_INTERVAL_MS) {
        lastLaunchTime = now;
        bool safe = plausibilityValidator.isCurrentlyValid() &&
                    canController.rxData().etcMode != CanEtcMode::MOTOR_OFF;
        launchController.update(safe && canController.rxData().launchActive);
    }
#endif

    // Auto-shifter — 毎イテレーション (passthrough レイテンシ最小化, パルス計時は内部 millis)
    // CAN ON かつ plausibility OK のときだけ auto。それ以外は OFF(manual=ドライバー入力スルー整形)。
    {
        bool autoOn = canController.rxData().autoShiftActive && plausibilityValidator.isCurrentlyValid();
        autoShifter.update(autoOn);
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
