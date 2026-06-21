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
#ifdef ADC_DMA
    // 非ブロッキング: 前回 DMA 結果を averages に反映し次の DMA を kick するだけ (~数 us)
    sensorHub.sampleAdcDmaIsr();
#endif
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

#ifdef ADC_DMA
    // ADC は DMA 駆動。8kHz ISR は非ブロッキング (DMA 結果の反映 + 次 kick のみ) なので
    // USB を枯渇させない。IMU は loop で読む (sensorHub.readImu)。
    sensorSamplingTimer.begin(sensorSamplingISR, SENSOR_SAMPLING_RATE_US);
    // USB(既定 ~128) より低い優先度にして、ISR が USB の送受信(コマンド処理)を
    // 阻害しないようにする。ISR は kick+反映のみの軽処理なのでサンプリングへの影響は小。
    // (motor ISR は priority 0 のまま最優先)
    sensorSamplingTimer.priority(208);
#else
    // 非DMA: sensorHub.read() (ADC ブロッキング + IMU) を loop() で呼ぶ。
    // 8kHz ISR でブロッキング SPI を回すと USB(低優先) を枯渇させポートが開けなくなるため
    // ISR 化はしない (DMA 化が前提)。
#endif

    plausibilityValidator.initialize();

    commandController.registerCommands(commandRouter);
}

unsigned long lastLogTime = 0;
unsigned long lastPulseUpdateTime = 0;
unsigned long lastCanTime = 0;
unsigned long lastLaunchTime = 0;

void loop() {
    unsigned long now = millis();

#ifdef ADC_DMA
    sensorHub.readImu();  // ADC は 8kHz DMA ISR でサンプリング済み。loop は IMU のみ
#else
    sensorHub.read();     // 非DMA: ADC(ブロッキング) + sensor.update + IMU を loop で
#endif

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
