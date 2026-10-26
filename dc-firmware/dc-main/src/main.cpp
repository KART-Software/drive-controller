#include <Arduino.h>
#include <TimeLib.h>
#include <kart_can.h>

#include "can/can_controller.hpp"
#include "can/flexcan3_bus.hpp"
#include "commands/command_controller.hpp"
#include "commands/command_router.hpp"
#include "configurator.hpp"
#include "control/control_input.hpp"
#include "constants.hpp"
#include "etc/experiment_runner.hpp"
#include "etc/motor_controller.hpp"
#include "etc/plausibility_validator.hpp"
#include "launch/launch_controller.hpp"
#include "log_record_builder.hpp"
#include "sensor/sensor_hub.hpp"
#include "shift/auto_shifter.hpp"
#include "starter/starter_controller.hpp"
#include "serial/serial_debug_writer.hpp"
#include "serial/serial_protocol.hpp"
#include "util/boot_mode.hpp"
#include "util/flash.hpp"
#include "util/log/debug_logger.hpp"
#include "util/log/debug_print.hpp"
#include "util/log/log_ring.hpp"
#include "util/log/sd_log_store.hpp"
#include "util/log/sensor_logger.hpp"
#include "util/every.hpp"
#include "util/flash_write_gate.hpp"
#include "util/gpt_timer.hpp"
#include "util/loop_stats.hpp"
#include "util/stop_watch.hpp"
#include "util/system_clock.hpp"

// 実行コンテキスト (tickISR / canRxISR / sensorSamplingISR / loop) と優先度は docs/loop_nonblocking_spec.md §3.1 と constants.hpp

// 参照を渡す部品より前、かつ IntervalTimer より前に定義する (static 初期化は定義順。理由は util/system_clock.hpp)
SystemClock sysClock;
Gpt2Timer tickTimer;
IntervalTimer sensorSamplingTimer;
FlashWriteGate flashGate(sysClock);
volatile uint32_t tickCount = 0;

Flash flash;
SensorHub sensorHub(sysClock);
Flexcan3Bus canBus;
CanController canController(sensorHub.view(), canBus);
// ドライバー指令はここだけを通す (CAN 0x740 / 0x741 と console の上書き)。使う側は入力元を知らない
ControlInput controlInput(canController.rxData());

etc::PlausibilityValidator plausibilityValidator(sensorHub.view().apps1(),
                                                 sensorHub.view().apps2(),
                                                 sensorHub.ittr(),
                                                 sensorHub.view().tps1(),
                                                 sensorHub.view().tps2(),
                                                 sensorHub.target(),
                                                 sensorHub.bps());
etc::MotorController motorController(sensorHub.target(), sensorHub.view().tps1());
launch::LaunchController launchController(sensorHub.view().pulseEngine(),
                                          sensorHub.view().pulseWheelRL(),
                                          sensorHub.view().pulseWheelRR(),
                                          sensorHub.view().clutch(),
                                          flash);
shift::AutoShifter autoShifter(sensorHub.view().pulseEngine(),
                               sensorHub.view().pulseWheelFL(),
                               sensorHub.view().pulseWheelFR(),
                               sensorHub.view().gps(),
                               sensorHub.view().apps1());
starter::StarterController starterController(sensorHub.view().pulseEngine());
// リング本体 (≈ 360 KB) は RAM1 に入らないので RAM2
DMAMEM LogRecord logRingBuf[LogRing::kCapacity];
LogRing logRing(logRingBuf);
SdLogStore sdStore;
LoopStats loopStats(sysClock);
SensorLogger sensorLogger(sdStore, logRing, loopStats, sysClock);
// ベンチ専用 (docs/etc_experiment_mode_spec.md)。目標は manual target に注入し、コーストは ETC アーミング層が
// wantsCoast() を見て行う (モーターを直接は触らない)。SD ログが必須なので logger を持つ
etc::ExperimentRunner experimentRunner(sensorHub.view(),
                                       controlInput,
                                       sensorHub.target(),
                                       motorController,
                                       plausibilityValidator,
                                       sensorLogger,
                                       sysClock);
Configurator configurator(flash,
                          sensorHub,
                          motorController,
                          plausibilityValidator,
                          launchController,
                          autoShifter,
                          starterController);
CommandRouter commandRouter;
CommandController commandController(configurator,
                                    motorController,
                                    sensorHub.target(),
                                    experimentRunner,
                                    controlInput,
                                    sensorLogger,
                                    sysClock);

void sensorSamplingISR() {
    sensorHub.sampleDmaIsr();  // 前回の DMA 結果を反映して次を kick するだけ (数 µs)。ここで SPI を待たない
}

// tickISR からのみ。setMotorOn() が pid.reset() を含むので、SHUTDOWN からの再開で PID のワインドアップが残らない
void startEtc() {
    if (motorController.isOn())
        return;
    motorController.setMotorOn();
}
void stopEtc() {
    if (!motorController.isOn())
        return;
    motorController.setMotorOff();
}

// ── ETC アーミング / SHUTDOWN 安全層 (tickISR) ─────────────────────────────
// ETC を止める要因は独立に 5 つあり、SHUTDOWN_RELAY(点火系, pin2)を落とすのは①だけ。
//   ① プラウシビリティ違反 : ETC 停止 + SHUTDOWN_RELAY=LOW + 復帰不可(ラッチ)
//   ② SIG_IN=LOW (外部)     : ETC 停止 / RELAY=HIGH 維持 / SIG_IN=HIGH 復帰で ETC 再開
//   ③ MOTOR_OFF モード      : ETC 停止 / RELAY=HIGH (落とさない)。CAN 0x740 (または console の上書き) が
//                             別モードになれば再開する
//   ④ 同定実験リリース試験のコースト区間
//   ⑤ flash 書き込み (§6.8)  : 書き込みの間 (約 150 ms) は割り込みが止まりモーター制御も止まるので、
//                             先に ETC を止める。シフトのパルス中は待つ (止まった tick の中でパルスが伸びるため)
// ① が RELAY を LOW にすると AND 回路が開いて SIG_IN も LOW になるが、① ラッチを
// 最優先で判定するので ②(復帰可) の経路には入らない = 復帰不可を維持する。
// ここで落とすのは点火系 SHUTDOWN リレーであって、DcMotor が持つモーター電源リレー
// (DC_MOTOR_RELAY_PIN=pin3) とは別系統。
// ① のラッチは PlausibilityValidator::isValid() 自身が保持する (一度でも違反すると
// 永続 false、起動後 500ms は猶予で常に valid) ので、専用フラグは持たず直接判定する。
void armEtc(EtcTarget::Mode ctrlMode, uint32_t now) {
    if (!plausibilityValidator.isValid(now)) {
        digitalWrite(SHUTDOWN_RELAY_PIN, LOW);  // ① 点火系を遮断 (恒久)
        stopEtc();
    } else if (!sensorHub.view().shutdownSig().isOn() || ctrlMode == EtcTarget::Mode::MotorOff) {
        stopEtc();  // ② SIG_IN=LOW または ③ MOTOR_OFF → ETC 停止 (RELAY は HIGH のまま)
    } else if (experimentRunner.wantsCoast()) {
        stopEtc();  // ④ (①②③が常に優先)
    } else if (!flashGate.requested()) {
        startEtc();  // SIG_IN=HIGH かつ違反なしかつ MOTOR_OFF でない → ETC 動作 (停止中なら再開)
    }
    // ⑤ 新しいパルスはシフター側で止め (suspend)、出ているパルスが終わってから止めて許可する
    if (flashGate.requested() && autoShifter.state() != shift::AutoShifter::State::Pulsing) {
        stopEtc();
        flashGate.grant();
    }
}

// buildLogRecord が読む相手。参照の束なので一度作れば変わらない
const LogSources logSources = {
    .hub = sensorHub.view(),
    .plausibility = plausibilityValidator,
    .control = controlInput,
    .shifter = autoShifter,
    .motor = motorController,
    .experiment = experimentRunner,
    .starter = starterController,
    .can = canController,
};

// 1 ms ごと (GPT2、優先度 64)。時間の決まった処理はすべてここ (docs/loop_nonblocking_spec.md §6.2)。
// ISR から Serial / SD / flash / delay を呼ばないこと (debug は DebugLogger::log でよい。USB への出力は
// SerialDebugWriter がキューにためて loop で送る)。
void tickISR() {
    const StopWatch elapsed(sysClock);
    const uint64_t nowUs64 = sysClock.nowUs64();  // この tick の中はこの時刻で統一する
    const uint32_t nowUs = (uint32_t)nowUs64;
    const uint32_t now = (uint32_t)(nowUs64 / 1000);
    const uint32_t n = tickCount + 1;
    tickCount = n;

    // 1〜3 入力とモード
    sensorHub.readShutdownSig(now);
    controlInput.update(now);
    const EtcTarget::Mode ctrlMode = controlInput.etcMode();
    sensorHub.target().setMode(ctrlMode);  // MotorOff の実際の停止はアーミングで行う

    // 4〜5 プラウシビリティとアーミング → 6 モーター制御
    armEtc(ctrlMode, now);
    if (motorController.isOn())
        motorController.cycle(nowUs);

    // 7 同定実験。コーストの要求は次の tick のアーミングが拾う
    experimentRunner.tick(now);

    // 8 シフター。flash 書き込み待ちの間は新しいパルスを出さない (止まった tick の中でパルスが伸びる)
    autoShifter.setSuspended(flashGate.requested());
    autoShifter.tick(
        {
            .autoOn = controlInput.autoShiftOn(now),
            .upPressed = controlInput.shiftUpPressed(now),
            .downPressed = controlInput.shiftDownPressed(now),
        },
        nowUs);
    starterController.tick(controlInput.starterPressed(now), now);

    // 9 回転計測
    sensorHub.updatePulse(nowUs64);

    // 10 CAN 送信。周期は kart-can の定義 (can.yaml の cycle_ms) どおり
    if (n % KART_CAN_DC_GYRO_XY_CYCLE_TIME_MS == 0)
        canController.sendImu();
    // 0x604/0x605/0x607/0x608/0x609 (同じ 16 ms 周期)。0x600 系と半周期ずらして、1 回に書くフレーム数とバスの負荷をならす
    static_assert(KART_CAN_DC_PEDALS_CYCLE_TIME_MS == KART_CAN_DC_GYRO_XY_CYCLE_TIME_MS, "半周期ずらしは同じ周期が前提");
    if (n % KART_CAN_DC_PEDALS_CYCLE_TIME_MS == KART_CAN_DC_PEDALS_CYCLE_TIME_MS / 2)
        canController.sendSensors();
    if (n % KART_CAN_DC_STATUS_CYCLE_TIME_MS == 0) {
        canController.sendStatus({
            .etcMode = sensorHub.view().target().getMode(),
            .launchActive = false,
            .autoShift = controlInput.autoShiftOn(now),
            .starterRelay = starterController.relayOn(),
            .shutdownLoopClosed = sensorHub.view().shutdownSig().isOn(),
        });
    }

    // 11 SD ログの採取 (書くのは loop)
    sensorLogger.capture([&] { return buildLogRecord(nowUs64, now, logSources, loopStats.live()); });

    loopStats.noteTickUs(elapsed.elapsedUs());
}

// FlexCAN3 の受信割り込み (優先度 64 = tickISR と同じなので互いに割り込まない)。
// 即時に処理すべき入力 (0x741 のシフトパドル) は受けたその場でシフターへ渡す (tick を待たない)
void canRxISR(const CanFrame& frame) {
    const uint32_t now = sysClock.nowMs();
    canController.receive(frame, now);
    if (needsImmediateHandling(frame)) {
        const CanRxData& rx = canController.rxData();
        autoShifter.onPaddles(rx.shiftUp, rx.shiftDown, sysClock.nowUs());  // パルスの開始時刻 = 出力を立てた時刻
    }
}

// loop() の周期処理 (時間の決まった処理は tickISR。loop に残すのは遅れてよいものだけ)
Every statsWindow{1000};  // 起点は setup() 末尾
Every launchTick{LAUNCH_UPDATE_INTERVAL_MS};
Every telemetry{SENSOR_SEND_INTERVAL};

void setup() {
    DebugLogger::initialize(sysClock);

    static SerialDebugWriter serialDebugWriter;
    DebugLogger::addWriter(&serialDebugWriter);

    // 直前の起動が fault で終わっていれば、コアが RAM に残した CrashReport を debug に出す
    if (CrashReport) {
        DebugPrint p;
        CrashReport.printTo(p);
        p.flush();
    }

    // SD リーダーへの分岐は dc-boot で済んでいる。ここでは理由を debug に出して消すだけ
    boot_mode::logAndClear();

    // RTC を TimeLib に供給 (VBAT 保持されていれば起動時から日時が使える。set_rtc で更新)。
    // Teensy3Clock.get は unsigned long (32bit) 返しだが TimeLib の time_t は 64bit なので、
    // 関数ポインタを直接渡さずラムダで型を合わせる (直接渡しは -fpermissive 依存 + 上位 32bit 不定)。
    setSyncProvider([]() -> time_t { return Teensy3Clock.get(); });

    // 点火系の許可。落とすのは armEtc の ① だけ
    pinMode(SHUTDOWN_RELAY_PIN, OUTPUT);
    digitalWrite(SHUTDOWN_RELAY_PIN, HIGH);

    flash.initialize();
    flash.setWriteGate(&flashGate);
    configurator.calibrateFromFlash();  // ハードウェアには触らない (センサーの begin はこの後)

    // センサーのハードウェアは config の後に動かす (回転センサーは config の歯数・窓・両エッジで動き出す)
    sensorHub.begin();
#if defined(LAUNCH_CONTROL_ENABLED)
    launchController.begin();
#endif
    autoShifter.begin();
    starterController.begin();

    // 起動直後の既定モード。0x740 受信前は ControlInput が MOTOR_OFF を返すので、最初の tick で
    // MotorOff に切り替わり、アーミング③が ETC を止めたまま待つ
    sensorHub.target().setMode(EtcTarget::Mode::MotorOff);
    motorController.initialize();

    // CAN は受信割り込みがシフターを呼ぶので、シフターの初期化の後で始める
    if (!canController.begin(canRxISR, CAN_RX_ISR_PRIORITY))
        DebugLogger::log("CAN: unsupported config (bitrate / FD)");

    sensorSamplingTimer.begin(sensorSamplingISR, SENSOR_SAMPLING_RATE_US);
    // USB (128) より低くして、16 kHz の ISR がコマンドの送受信を阻害しないようにする。PIT の割り込みはこのタイマー
    // だけが使うので (チャネル 0 / 1 は sysClock が割り込みなしで使う)、この優先度がそのまま効く
    sensorSamplingTimer.priority(SENSOR_SAMPLING_ISR_PRIORITY);

    commandController.registerCommands(commandRouter);

    sensorLogger.begin();

    // 計測窓の起点 (setup() の所要は計測しない。最初の窓は最初の loop から 1 s)
    statsWindow.restart(sysClock.nowMs());

    // プラウシビリティの起点は初期化の最後 (tick を始める直前) にする。途中 (SD の初期化など) で時間がかかっても、
    // 最初の tick から継続時間の判定 (SENSOR_IMPLAUSIBLE_THRESHOLD_TIME など) が正しく効く
    plausibilityValidator.initialize(sysClock.nowMs());

    // 最後に tick を始める (以後止めない)。ここから flash 書き込みは tick の許可を待つ
    tickTimer.begin(TICK_PERIOD_US, tickISR, TICK_ISR_PRIORITY);
    flashGate.enable();
}

void loop() {
    loopStats.tick();
    const uint32_t now = sysClock.nowMs();
    // 計測窓の確定 (1 s)。tick 直後に置くので、直前の周 (= tick が計上した周) とその周の SD 所要が
    // 同じ窓に入る (末尾に置くと SD 所要だけ 1 窓先行して組み合わせがずれる)
    if (statsWindow.due(now)) {
        loopStats.rollover(sensorLogger.drops());
    }

#if defined(LAUNCH_CONTROL_ENABLED)
    // Launch FSM tick — 20Hz (pulse counter 周期 100ms と整合)
    // Plausibility 違反 / MOTOR_OFF 時は launchRequested を強制 false にして
    // FSM を Idle に戻す (handler 側の !launchRequested パスで motor_.off() 経由)。
    if (launchTick.due(now)) {
        bool safe = plausibilityValidator.currentlyValid() && controlInput.etcMode() != EtcTarget::Mode::MotorOff;
        launchController.update(safe && controlInput.launchOn(now), now);
    }
#endif

    if (telemetry.due(now)) {
        SerialProtocol::sendSensorData(sensorHub.view(), plausibilityValidator.validLatched(),
                                       plausibilityValidator.getErrorHandler(), motorController.lastOutput(),
                                       loopStats.live(), controlInput, canController.busState(), canController.busStats(), now);
    }

    sensorLogger.service(now);  // ここで loop が止まってもリング (≈ 2 s) が吸収する
    DebugLogger::service();  // ISR が積んだ debug を送る
    commandRouter.poll();
}
