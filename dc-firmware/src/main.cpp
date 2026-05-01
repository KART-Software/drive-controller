#include <Arduino.h>

#include "can_bus.hpp"
#include "commands/command_controller.hpp"
#include "commands/command_router.hpp"
#include "configurator.hpp"
#include "constants.hpp"
#include "error_handler.hpp"
#include "motor_controller.hpp"
#include "plausibility_validator.hpp"
#include "sensors.hpp"
#include "serial_protocol.hpp"

// IMU driver selection
#include "imu.hpp"
#ifdef USE_ICM45686
#include "icm45686.hpp"
#endif

#include "pulse_counter.hpp"

IntervalTimer motorControlTimer;
IntervalTimer sensorSamplingTimer;

CanBus canBus;
Apps apps1(APPS_1_RAW_MIN, APPS_1_RAW_MAX, APPS_1_CH);
Apps apps2(APPS_2_RAW_MIN, APPS_2_RAW_MAX, APPS_2_CH);
Tps tps1(TPS_1_RAW_MIN, TPS_1_RAW_MAX, TPS_1_CH);
Tps tps2(TPS_2_RAW_MIN, TPS_2_RAW_MAX, TPS_2_CH);
Ittr ittr = Ittr();
Bps bps = Bps();
Target target(apps1, ittr);

PlausibilityValidator plausibilityValidator(apps1, apps2, ittr, tps1, tps2, target, bps);
MotorController motorController(target, tps1);
Configurator configurator(apps1, apps2, tps1, tps2, ittr, target, motorController, plausibilityValidator);
CommandRouter commandRouter;
CommandController commandController(configurator, motorController, target);

// IMU instance
#ifdef USE_ICM45686
Icm45686 imu(IMU_CS_PIN);
#endif

// Pulse counters (wheel speed + engine RPM)
PulseCounter pulseWheelFL(PULSE_WHEEL_FL_PIN);
PulseCounter pulseWheelFR(PULSE_WHEEL_FR_PIN);
PulseCounter pulseWheelRL(PULSE_WHEEL_RL_PIN);
PulseCounter pulseWheelRR(PULSE_WHEEL_RR_PIN);
PulseCounter pulseEngine(PULSE_ENGINE_PIN);

volatile bool motorTimerRunning = false;

void motorControlISR() {
  motorController.cycle();
}

void sensorSamplingISR() {
  gAdc.read();
  apps1.read();
  apps2.read();
  ittr.read();
  tps1.read();
  tps2.read();
  bps.read();
#if defined(USE_ICM45686)
  imu.read();
#endif
}

void setup() {
  SerialProtocol::initialize();

  pinMode(FUEL_PUMP_PIN, OUTPUT);
  digitalWrite(FUEL_PUMP_PIN, HIGH);
  gAdc.begin();
#if defined(USE_ICM45686)
  imu.begin();
#endif

  // Initialize pulse counters
  pulseWheelFL.begin();
  pulseWheelFR.begin();
  pulseWheelRL.begin();
  pulseWheelRR.begin();
  pulseEngine.begin();

  canBus.begin();
  configurator.initialize();
  configurator.calibrateFromFlash();

  // Default mode until CAN mode-select frame is received
  target.setModeNormal();
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
  // IntervalTimer uses PIT channels; priorities set via NVIC_SET_PRIORITY for PIT IRQs
  if (motorTimerRunning) {
    // PIT timers on Teensy 4.x share IRQ_PIT, but IntervalTimer uses individual channels
    // Set motor timer priority higher (lower number = higher priority)
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
        target.setModeCalibration();
        break;
      case 1:
        target.setModeNormal();
        break;
      case 2:
        target.setModeRestricted();
        break;
      default:
        break;
    }
  }

  // Update pulse counters periodically
  if (now - lastPulseUpdateTime >= PULSE_UPDATE_INTERVAL_MS) {
    lastPulseUpdateTime = now;
    pulseWheelFL.update();
    pulseWheelFR.update();
    pulseWheelRL.update();
    pulseWheelRR.update();
    pulseEngine.update();
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
    canBus.sendThrottleFrame(apps1.convertedValue(), apps2.convertedValue(), tps1.convertedValue(),
                             tps2.convertedValue());
    canBus.sendStatusFrame(target.getTarget(), bps.convertedValue(), plausibilityValidator.isValid() ? 0x0000 : 0x0001,
                           canBus.modeStringToId(target.getModeString()));
  }
  // CAN TX — slow frames (wheel speed, engine/IMU, gyro) every 100ms
  if (now - lastCanSlowTime >= CAN_TX_SLOW_INTERVAL_MS) {
    lastCanSlowTime = now;
    canBus.sendWheelSpeedFrame(pulseWheelFL.getFrequencyHz(), pulseWheelFR.getFrequencyHz(),
                               pulseWheelRL.getFrequencyHz(), pulseWheelRR.getFrequencyHz());
#if defined(USE_ICM45686)
    canBus.sendEngineImuFrame(pulseEngine.getFrequencyHz(), imu.accel[0], imu.accel[1], imu.accel[2]);
    canBus.sendGyroFrame(imu.gyro[0], imu.gyro[1], imu.gyro[2]);
#else
    canBus.sendEngineImuFrame(pulseEngine.getFrequencyHz(), 0, 0, 0);
#endif
  }

  // Send sensor data via JSON protocol (50Hz)
  if (now - lastLogTime >= SENSOR_SEND_INTERVAL) {
    lastLogTime = now;
#if defined(USE_ICM45686)
    SerialProtocol::sendSensorData(apps1, apps2, ittr, tps1, tps2, bps, target, plausibilityValidator.isValid(),
                                   plausibilityValidator.getErrorHandler(), gAdc.sps(), imu.accel[0], imu.accel[1],
                                   imu.accel[2], imu.gyro[0], imu.gyro[1], imu.gyro[2],
                                   pulseWheelFL.getFrequencyHz(), pulseWheelFR.getFrequencyHz(),
                                   pulseWheelRL.getFrequencyHz(), pulseWheelRR.getFrequencyHz(),
                                   pulseEngine.getFrequencyHz());
#else
    SerialProtocol::sendSensorData(apps1, apps2, ittr, tps1, tps2, bps, target, plausibilityValidator.isValid(),
                                   plausibilityValidator.getErrorHandler(), gAdc.sps(), 0, 0, 0, 0, 0, 0,
                                   pulseWheelFL.getFrequencyHz(), pulseWheelFR.getFrequencyHz(),
                                   pulseWheelRL.getFrequencyHz(), pulseWheelRR.getFrequencyHz(),
                                   pulseEngine.getFrequencyHz());
#endif
  }

  // Command polling
  commandRouter.poll();
}
