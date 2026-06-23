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

void motorControlISR() {
    motorController.cycle();
}

void sensorSamplingISR() {
#ifdef ADC_DMA
    // 非ブロッキング: 前回 DMA 結果を averages に反映し次の DMA を kick するだけ (~数 us)
    sensorHub.sampleAdcDmaIsr();
#endif
}

// ETC (モーター制御) を開始/停止する。冪等 (既にその状態なら何もしない)。
//   停止 = setMotorOff() (PWM=0 + モーター電源リレー/SLP を LOW) + モーター ISR 停止。
//   開始 = setMotorOn() (内部で pid.reset() = PID ワインドアップ解消) + モーター ISR 再起動。
// SHUTDOWN による停止→再開はこの 2 つで行うため、再開時に PID は必ずリセットされる。
// IntervalTimer に稼働状態の getter は無いが、タイマーとモーターは必ずここで同時に
// 切り替えるので motorController.isOn() を「ETC 稼働中か」の単一の真実とする。
void startEtc() {
    if (motorController.isOn())
        return;
    motorController.setMotorOn();  // pid.reset() を含む
    motorControlTimer.begin(motorControlISR, MOTOR_CONTROLL_CYCLE_TIME * 1000);  // ms -> us
    motorControlTimer.priority(0);  // motor ISR を最高優先
}
void stopEtc() {
    if (!motorController.isOn())
        return;
    motorController.setMotorOff();  // 先に PWM=0/出力停止 (cycle の write は _isOn=false で no-op)
    motorControlTimer.end();        // その後 ISR を止める
}

void setup() {
    SerialProtocol::initialize();

    static SerialDebugWriter serialDebugWriter;
    DebugLogger::addWriter(&serialDebugWriter);

    // SHUTDOWN 回路: 通常 RELAY=HIGH (点火系許可)。SIG_IN は SensorHub が ToggleSwitch で読む。
    pinMode(SHUTDOWN_RELAY_PIN, OUTPUT);
    digitalWrite(SHUTDOWN_RELAY_PIN, HIGH);

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
    // モーターは起動時に回さない。SIG_IN=HIGH かつプラウシビリティ違反ラッチなし
    // かつ MOTOR_OFF でない場合に loop() の ETC アーミングが startEtc() で ON にする。

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
            // target モードのみ設定。実際の ETC 停止は下の ETC アーミングで行う。
            sensorHub.mut.target().setModeMotorOff();
            break;
    }

    // Update pulse counters periodically
    if (now - lastPulseUpdateTime >= PULSE_UPDATE_INTERVAL_MS) {
        lastPulseUpdateTime = now;
        sensorHub.updatePulse();
    }

    // ── ETC アーミング / SHUTDOWN 安全層 ─────────────────────────────────
    // ETC を止める要因は独立に 3 つあり、SHUTDOWN_RELAY(点火系, pin2)を落とすのは①だけ。
    //   ① プラウシビリティ違反 : ETC 停止 + SHUTDOWN_RELAY=LOW + 復帰不可(ラッチ)
    //   ② SIG_IN=LOW (外部)     : ETC 停止 / RELAY=HIGH 維持 / SIG_IN=HIGH 復帰で ETC 再開
    //   ③ CAN MOTOR_OFF モード  : ETC 停止 / RELAY=HIGH (落とさない)
    // ① が RELAY を LOW にすると AND 回路が開いて SIG_IN も LOW になるが、① ラッチを
    // 最優先で判定するので ②(復帰可) の経路には入らない = 復帰不可を維持する。
    // ここで落とすのは点火系 SHUTDOWN リレーであって、DcMotor が持つモーター電源リレー
    // (DC_MOTOR_RELAY_PIN=pin3) とは別系統。
    // ① のラッチは PlausibilityValidator::isValid() 自身が保持する (一度でも違反すると
    // 永続 false、起動後 500ms は猶予で常に valid) ので、専用フラグは持たず直接判定する。
    // SIG_IN は SensorHub が毎ループ read 済みなのでここでは状態を読むだけ。
    if (!plausibilityValidator.isValid()) {
        digitalWrite(SHUTDOWN_RELAY_PIN, LOW);  // ① 点火系を遮断 (恒久)
        stopEtc();
    } else if (!sensorHub.shutdownSig().isOn() ||
               canController.rxData().etcMode == CanEtcMode::MOTOR_OFF) {
        stopEtc();  // ② SIG_IN=LOW または ③ MOTOR_OFF → ETC 停止 (RELAY は HIGH のまま)
    } else {
        startEtc();  // SIG_IN=HIGH かつ違反なしかつ MOTOR_OFF でない → ETC 動作 (停止中なら再開)
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
