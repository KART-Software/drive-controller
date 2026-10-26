#include <Arduino.h>
#include <TimeLib.h>

#include "app/loop.hpp"
#include "can/can_controller.hpp"
#include "can/flexcan3_bus.hpp"
#include "commands/command_controller.hpp"
#include "commands/command_router.hpp"
#include "configurator.hpp"
#include "constants.hpp"
#include "control/control_input.hpp"
#include "etc/experiment_runner.hpp"
#include "etc/motor_controller.hpp"
#include "etc/plausibility_validator.hpp"
#include "isr/can_isr.hpp"
#include "isr/sensor_sampling_isr.hpp"
#include "isr/tick_isr.hpp"
#include "launch/launch_controller.hpp"
#include "log_record_builder.hpp"
#include "sensor/sensor_hub.hpp"
#include "serial/serial_debug_writer.hpp"
#include "serial/serial_protocol.hpp"
#include "shift/auto_shifter.hpp"
#include "starter/starter_controller.hpp"
#include "util/boot_mode.hpp"
#include "util/every.hpp"
#include "util/flash.hpp"
#include "util/flash_write_gate.hpp"
#include "util/gpt_timer.hpp"
#include "util/log/debug_logger.hpp"
#include "util/log/debug_print.hpp"
#include "util/log/log_ring.hpp"
#include "util/log/sd_log_store.hpp"
#include "util/log/sensor_logger.hpp"
#include "util/loop_stats.hpp"
#include "util/system_clock.hpp"

// 部品の生成と配線だけ (docs/loop_nonblocking_spec.md §3.3)。割り込みの入口は isr/、ロジックは app/

// リング本体 (≈ 360 KB) は RAM1 に入らないので RAM2。section 属性は構造体のメンバーに付けられないので Parts の外
DMAMEM LogRecord logRingBuf[LogRing::kCapacity];

namespace {

// 部品のすべて。メンバーの定義順 = 生成順で、後のメンバーは前のメンバーの参照を受け取る。
// sysClock は先頭 (参照を渡す部品より前、かつ IntervalTimer より前。理由は util/system_clock.hpp)
struct Parts {
    SystemClock sysClock;
    Gpt2Timer tickTimer;
    IntervalTimer sensorSamplingTimer;
    FlashWriteGate flashGate{sysClock};

    Flash flash;
    SensorHub sensorHub{sysClock};
    Flexcan3Bus canBus;
    CanController canController{sensorHub.view(), canBus};
    // ドライバー指令はここだけを通す (CAN 0x740 / 0x741 と console の上書き)。使う側は入力元を知らない
    ControlInput controlInput{canController.rxData()};

    etc::PlausibilityValidator plausibilityValidator{sensorHub.view().apps1(),
                                                     sensorHub.view().apps2(),
                                                     sensorHub.ittr(),
                                                     sensorHub.view().tps1(),
                                                     sensorHub.view().tps2(),
                                                     sensorHub.target(),
                                                     sensorHub.bps()};
    etc::MotorController motorController{sensorHub.target(), sensorHub.view().tps1()};
    launch::LaunchController launchController{sensorHub.view().pulseEngine(),
                                              sensorHub.view().pulseWheelRL(),
                                              sensorHub.view().pulseWheelRR(),
                                              sensorHub.view().clutch(),
                                              flash};
    shift::AutoShifter autoShifter{sensorHub.view().pulseEngine(),
                                   sensorHub.view().pulseWheelFL(),
                                   sensorHub.view().pulseWheelFR(),
                                   sensorHub.view().gps(),
                                   sensorHub.view().apps1()};
    starter::StarterController starterController{sensorHub.view().pulseEngine()};
    LogRing logRing{logRingBuf};
    SdLogStore sdStore;
    LoopStats loopStats{sysClock};
    SensorLogger sensorLogger{sdStore, logRing, loopStats, sysClock};
    // ベンチ専用 (docs/etc_experiment_mode_spec.md)。目標は manual target に注入し、コーストは ETC アーミング層が
    // wantsCoast() を見て行う (モーターを直接は触らない)。SD ログが必須なので logger を持つ
    etc::ExperimentRunner experimentRunner{sensorHub.view(),
                                           controlInput,
                                           sensorHub.target(),
                                           motorController,
                                           plausibilityValidator,
                                           sensorLogger,
                                           sysClock};
    Configurator configurator{flash,
                              sensorHub,
                              motorController,
                              plausibilityValidator,
                              launchController,
                              autoShifter,
                              starterController};
    CommandRouter commandRouter;
    CommandController commandController{configurator,
                                        motorController,
                                        sensorHub.target(),
                                        experimentRunner,
                                        controlInput,
                                        sensorLogger,
                                        sysClock};
    SerialDebugWriter serialDebugWriter;

    // loop の周期 (起点は setup() 末尾)。状態なので app::loop ではなくここが持つ
    Every statsWindow{1000};
    Every launchTick{LAUNCH_UPDATE_INTERVAL_MS};
    Every telemetry{SENSOR_SEND_INTERVAL};

    // 各文脈のロジックに渡す参照の束
    const LogSources logSources{
        .hub = sensorHub.view(),
        .plausibility = plausibilityValidator,
        .control = controlInput,
        .shifter = autoShifter,
        .motor = motorController,
        .experiment = experimentRunner,
        .starter = starterController,
        .can = canController,
    };
    const TickDeps tickDeps{
        .clock = sysClock,
        .hub = sensorHub,
        .control = controlInput,
        .plausibility = plausibilityValidator,
        .motor = motorController,
        .experiment = experimentRunner,
        .shifter = autoShifter,
        .starter = starterController,
        .can = canController,
        .logger = sensorLogger,
        .stats = loopStats,
        .flashGate = flashGate,
        .logSources = logSources,
    };
    const CanRxDeps canRxDeps{.clock = sysClock, .can = canController, .shifter = autoShifter};
    const SensorSamplingDeps samplingDeps{.hub = sensorHub};
    const LoopDeps loopDeps{
        .clock = sysClock,
        .hub = sensorHub.view(),
        .plausibility = plausibilityValidator,
        .motor = motorController,
        .control = controlInput,
        .can = canController,
        .launch = launchController,
        .logger = sensorLogger,
        .stats = loopStats,
        .router = commandRouter,
        .statsWindow = statsWindow,
        .launchTick = launchTick,
        .telemetry = telemetry,
    };
};

Parts parts;

}  // namespace

void setup() {
    // 割り込みの入口に依存を渡す (タイマーと CAN はこの後で始める)
    setTickDeps(parts.tickDeps);
    setCanRxDeps(parts.canRxDeps);
    setSensorSamplingDeps(parts.samplingDeps);

    DebugLogger::initialize(parts.sysClock);
    DebugLogger::addWriter(&parts.serialDebugWriter);

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

    // 点火系の許可。落とすのは app::tick のアーミング ① だけ
    pinMode(SHUTDOWN_RELAY_PIN, OUTPUT);
    digitalWrite(SHUTDOWN_RELAY_PIN, HIGH);

    parts.flash.initialize();
    parts.flash.setWriteGate(&parts.flashGate);
    parts.configurator.calibrateFromFlash();  // ハードウェアには触らない (センサーの begin はこの後)

    // センサーのハードウェアは config の後に動かす (回転センサーは config の歯数・窓・両エッジで動き出す)
    parts.sensorHub.begin();
#if defined(LAUNCH_CONTROL_ENABLED)
    parts.launchController.begin();
#endif
    parts.autoShifter.begin();
    parts.starterController.begin();

    // 起動直後の既定モード。0x740 受信前は ControlInput が MOTOR_OFF を返すので、最初の tick で
    // MotorOff に切り替わり、アーミング③が ETC を止めたまま待つ
    parts.sensorHub.target().setMode(EtcTarget::Mode::MotorOff);
    parts.motorController.initialize();

    // CAN は受信割り込みがシフターを呼ぶので、シフターの初期化の後で始める
    if (!parts.canController.begin(canRxISR, CAN_RX_ISR_PRIORITY))
        DebugLogger::log("CAN: unsupported config (bitrate / FD)");

    parts.sensorSamplingTimer.begin(sensorSamplingISR, SENSOR_SAMPLING_RATE_US);
    // USB (128) より低くして、16 kHz の ISR がコマンドの送受信を阻害しないようにする。PIT の割り込みはこのタイマー
    // だけが使うので (チャネル 0 / 1 は sysClock が割り込みなしで使う)、この優先度がそのまま効く
    parts.sensorSamplingTimer.priority(SENSOR_SAMPLING_ISR_PRIORITY);

    parts.commandController.registerCommands(parts.commandRouter);

    parts.sensorLogger.begin();

    // 計測窓の起点 (setup() の所要は計測しない。最初の窓は最初の loop から 1 s)
    parts.statsWindow.restart(parts.sysClock.nowMs());

    // プラウシビリティの起点は初期化の最後 (tick を始める直前) にする。途中 (SD の初期化など) で時間がかかっても、
    // 最初の tick から継続時間の判定 (SENSOR_IMPLAUSIBLE_THRESHOLD_TIME など) が正しく効く
    parts.plausibilityValidator.initialize(parts.sysClock.nowMs());

    // 最後に tick を始める (以後止めない)。ここから flash 書き込みは tick の許可を待つ
    parts.tickTimer.begin(TICK_PERIOD_US, tickISR, TICK_ISR_PRIORITY);
    parts.flashGate.enable();
}

void loop() {
    app::loop(parts.loopDeps);
}
