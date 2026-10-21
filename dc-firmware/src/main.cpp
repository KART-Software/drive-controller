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
#include "util/flash.hpp"
#include "util/log/debug_logger.hpp"
#include "util/log/log_ring.hpp"
#include "util/log/sd_log_store.hpp"
#include "util/log/sensor_logger.hpp"
#include "util/every.hpp"
#include "util/flash_write_gate.hpp"
#include "util/gpt_timer.hpp"
#include "util/loop_stats.hpp"
#include "util/stop_watch.hpp"
#include "util/system_clock.hpp"

// 実行コンテキスト (docs/loop_nonblocking_spec.md §3.1)。優先度は constants.hpp
//   tickISR           GPT2 1 ms, 64  : 安全層・モーター制御・同定実験・シフター・回転計測・CAN 送信
//   canRxISR          FlexCAN3 受信, 64 : 0x740 / 0x741 の取り込み、シフト入力の即時処理
//   sensorSamplingISR PIT 62.5 µs, 208 : ADC の DMA
//   loop()            : IMU 読み・SD ログ・telemetry・コマンド・ISR からの debug 送信
// 時刻の実体 (util/system_clock.hpp、PIT Lifetime Timer)。作った時点 (static 初期化) が時刻の 0。
// 他の部品へは Clock (interfaces/util/clock.hpp) の参照か時刻の引数で渡す (extern 宣言は置かない)。
// 参照を渡す部品より前、かつ IntervalTimer より前に定義する (同じファイルの中は定義順に作られる)
SystemClock sysClock;
Gpt2Timer tickTimer;
IntervalTimer sensorSamplingTimer;
FlashWriteGate flashGate(sysClock);
volatile uint32_t tickCount = 0;

Flash flash;
SensorHub sensorHub(sysClock);
Flexcan3Bus canBus;  // CAN3 (pin 30 / 31) のドライバ (docs/can_bus_spec.md)
CanController canController(sensorHub.view(), canBus);
// ドライバー指令の唯一の入口 (CAN 0x740 / 0x741 + console override)。docs/control_input_spec.md
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
// セルモーターリレー (docs/starter_spec.md)。tickISR がセルスイッチとエンジン回転で駆動する
starter::StarterController starterController(sensorHub.view().pulseEngine());
// SD ログ (docs/loop_nonblocking_spec.md §7): tick がリングへ積み、loop がまとめて書く。リング本体 (303 KB) は RAM2
DMAMEM LogRecord logRingBuf[LogRing::kCapacity];
LogRing logRing(logRingBuf);
SdLogStore sdStore;
LoopStats loopStats(sysClock);  // loop/SD/tickISR の所要時間 (1 s 窓)。telemetry + SD ログへ
SensorLogger sensorLogger(sdStore, logRing, loopStats, sysClock);
// ETC 同定実験モード (ベンチ専用, docs/etc_experiment_mode_spec.md)。目標注入は
// manual target 経由、コーストは下の ETC アーミング層が wantsCoast() を見て行う。
// 開始/停止は console コマンド (前提条件は start() が検証、SD 必須なので logger 参照)。
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
    // 非ブロッキング: 前回 DMA 結果を averages に反映し次の DMA を kick するだけ (~数 us)
    sensorHub.sampleDmaIsr();
}

// ETC (モーター制御) を開始/停止する。冪等 (既にその状態なら何もしない)。tickISR からのみ呼ぶ。
//   停止 = setMotorOff() (PWM=0 + モーター電源リレー/SLP を LOW)。
//   開始 = setMotorOn() (内部で pid.reset() = PID ワインドアップ解消)。
// モーター制御は tickISR の中で「ON なら cycle()」なので、タイマーの開始・停止は要らない。
// SHUTDOWN による停止→再開はこの 2 つで行うため、再開時に PID は必ずリセットされる。
void startEtc() {
    if (motorController.isOn())
        return;
    motorController.setMotorOn();  // pid.reset() を含む
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

// SD ログの入力 (buildLogRecord が読む相手)。参照は起動時に一度だけ束ねる
const LogSources logSources = {
    .hub = sensorHub.view(),
    .plausibility = plausibilityValidator,
    .control = controlInput,
    .shifter = autoShifter,
    .motor = motorController,
    .experiment = experimentRunner,
    .starter = starterController,
};

// 1 ms ごと (GPT2、優先度 64)。時間の決まった処理はすべてここ (docs/loop_nonblocking_spec.md §6.2)。
// ISR から Serial / SD / flash / delay を呼ばないこと (debug は DebugLogger::log でよい。USB への出力は
// SerialDebugWriter がキューにためて loop で送る)。
void tickISR() {
    const StopWatch elapsed(sysClock);  // この tick の所要 (LoopStats tick_max_us)
    const uint64_t nowUs64 = sysClock.nowUs64();  // 時刻はこの tick の間ずっとこの値を使う (millis() / micros() は使わない)
    const uint32_t nowUs = (uint32_t)nowUs64;
    const uint32_t now = (uint32_t)(nowUs64 / 1000);
    const uint32_t n = tickCount + 1;
    tickCount = n;

    // 1〜3 入力とモード
    sensorHub.readShutdownSig(now);
    controlInput.update(now);  // 新しい 0x740 の取り込み、途絶時のフォールバック、上書きの自動解除
    const EtcTarget::Mode ctrlMode = controlInput.etcMode();
    sensorHub.target().setMode(ctrlMode);  // MotorOff の実際の停止はアーミングで行う

    // 4〜5 プラウシビリティとアーミング → 6 モーター制御
    armEtc(ctrlMode, now);
    if (motorController.isOn())
        motorController.cycle(nowUs);

    // 7 同定実験 (開始 / 停止の要求もここで取り込む)。コーストは次の tick のアーミングが拾う
    experimentRunner.tick(now);

    // 8 シフター (パルスの終端・最短間隔・自動判断)。flash 書き込み待ちの間は新しいパルスを出さない
    autoShifter.setSuspended(flashGate.requested());
    autoShifter.tick(
        {
            .autoOn = controlInput.autoShiftOn(now),
            .upPressed = controlInput.shiftUpPressed(now),
            .downPressed = controlInput.shiftDownPressed(now),
        },
        nowUs);
    // セルモーターリレー (押した瞬間に ON、エンジンがかかるか保持時間後にスイッチが離れたら OFF)
    starterController.tick(controlInput.starterPressed(now), now);

    // 9 回転計測 (車輪速・エンジン・クラッチ後)
    sensorHub.updatePulse(nowUs64);

    // 10 CAN 送信。周期は kart-can の定義 (can.yaml の cycle_ms) どおり
    if (n % KART_CAN_DC_GYRO_XY_CYCLE_TIME_MS == 0)  // 0x600-0x603 (4 フレームとも同じ周期)
        canController.send();
    if (n % KART_CAN_DC_STATUS_CYCLE_TIME_MS == 0) {  // 0x60A DC_Status
        canController.sendStatus({
            .etcMode = sensorHub.view().target().getMode(),
            .launchActive = false,
            .autoShift = controlInput.autoShiftOn(now),
            .starterRelay = starterController.relayOn(),
            .shutdownLoopClosed = sensorHub.view().shutdownSig().isOn(),
        });
    }

    // 11 ログ 1 レコード採取 → リング (loop が SD へ書く)。無効 (カードなし / 停止) なら SensorLogger が数えるだけ
    sensorLogger.capture([&] { return buildLogRecord(nowUs64, now, logSources, loopStats.live()); });

    // 12 自身の所要時間 (telemetry SysStats.tick_max_us / LogRecord tick_max_us)
    loopStats.noteTickUs(elapsed.elapsedUs());
}

// FlexCAN3 の受信割り込み (優先度 64 = tickISR と同じなので互いに割り込まない)。
// 即時に処理すべき入力 (0x741 のシフトパドル) は受けたその場でシフターへ渡す (tick を待たない)
void canRxISR(const CanFrame& frame) {
    const uint32_t now = sysClock.nowMs();  // 受信時刻
    canController.receive(frame, now);
    if (needsImmediateHandling(frame)) {
        const CanRxData& rx = canController.rxData();
        autoShifter.onPaddles(rx.shiftUp, rx.shiftDown, sysClock.nowUs());  // パルスの開始時刻 = 出力を立てた時刻
    }
}

// loop() の周期処理。何をどの周期で回すかの一覧
// (時間の決まった処理は tickISR。loop に残すのは遅れてよいものだけ)
Every statsWindow{1000};                        // 計測窓 (LoopStats) の確定。起点は setup() 末尾
Every launchTick{LAUNCH_UPDATE_INTERVAL_MS};    // Launch FSM (凍結中は未使用)
Every telemetry{SENSOR_SEND_INTERVAL};          // テレメトリ送信

void setup() {
    DebugLogger::initialize(sysClock);

    static SerialDebugWriter serialDebugWriter;
    DebugLogger::addWriter(&serialDebugWriter);

    // RTC を TimeLib に供給 (VBAT 保持されていれば起動時から日時が使える。set_rtc で更新)。
    // Teensy3Clock.get は unsigned long (32bit) 返しだが TimeLib の time_t は 64bit なので、
    // 関数ポインタを直接渡さずラムダで型を合わせる (直接渡しは -fpermissive 依存 + 上位 32bit 不定)。
    setSyncProvider([]() -> time_t { return Teensy3Clock.get(); });

    // SHUTDOWN 回路: 通常 RELAY=HIGH (点火系許可)。SIG_IN は SensorHub が ToggleSwitch で読む。
    pinMode(SHUTDOWN_RELAY_PIN, OUTPUT);
    digitalWrite(SHUTDOWN_RELAY_PIN, HIGH);

    flash.initialize();
    flash.setWriteGate(&flashGate);  // 書き込みの前後で ETC を止めて再開 (tick が動き出してから有効)
    configurator.calibrateFromFlash();  // config を各部品に適用 (ハードウェアには触らない)

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
    // モーターは起動時に回さない。SIG_IN=HIGH かつプラウシビリティ違反ラッチなし
    // かつ MOTOR_OFF でない場合に tickISR の ETC アーミングが startEtc() で ON にする。

    // CAN は受信割り込みがシフターを呼ぶので、シフターの初期化の後で始める
    if (!canController.begin(canRxISR, CAN_RX_ISR_PRIORITY))
        DebugLogger::log("CAN: unsupported config (bitrate / FD)");

    // ADC は DMA 駆動。16kHz ISR は非ブロッキング (DMA 結果の反映 + 次 kick のみ) なので
    // USB を枯渇させない。IMU も同じ ISR から IMU_SAMPLING_DIVIDER 回に 1 回 (8 kHz) DMA で読む。
    sensorSamplingTimer.begin(sensorSamplingISR, SENSOR_SAMPLING_RATE_US);
    // USB(既定 ~128) より低い優先度にして、ISR が USB の送受信(コマンド処理)を
    // 阻害しないようにする。ISR は kick+反映のみの軽処理なのでサンプリングへの影響は小。
    // PIT の割り込みはこのタイマーだけが使うので、この優先度がそのまま効く (tick は GPT2。チャネル 0 / 1 は
    // sysClock の Lifetime Timer が割り込みなしで使い、このタイマーはチャネル 2 になる)
    sensorSamplingTimer.priority(SENSOR_SAMPLING_ISR_PRIORITY);

    commandController.registerCommands(commandRouter);

    // SD ロギング: カード挿入時のみ有効 (exFAT でなければフォーマット)。起動ごとに dc_NNNN[_日時]/ を作る (docs/loop_nonblocking_spec.md §7)
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
    loopStats.tick();  // 前回 loop 先頭からの経過 = 1 周の所要 (停止の可視化)
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

    // Send sensor data via serial protocol (50Hz)
    if (telemetry.due(now)) {
        SerialProtocol::sendSensorData(sensorHub.view(), plausibilityValidator.validLatched(),
                                       plausibilityValidator.getErrorHandler(), motorController.lastOutput(),
                                       loopStats.live(), controlInput, canController.busState(), canController.busStats(), now);
    }

    // SD ロギング: tick がリングへ積んだレコードをまとめて書く (4 KB 以上で最大 8 KB、2 s ごとに sync、100 MB でファイル切り替え)。
    // ここで loop が止まってもリング (≈2 s) が吸収する。所要は LoopStats (telemetry SysStats.sd_max_us / LogRecord sd_max_us)
    sensorLogger.service(now);

    // ISR (tick / CAN 受信) が積んだ debug を送る
    DebugLogger::service();

    // Command polling
    commandRouter.poll();
}
