#include "app/tick.hpp"

#include <Arduino.h>
#include <kart_can.h>

#include "constants.hpp"
#include "util/stop_watch.hpp"

namespace app {
namespace {

// tick からのみ。setMotorOn() が pid.reset() を含むので、SHUTDOWN からの再開で PID のワインドアップが残らない
void startEtc(etc::MotorController& motor) {
    if (motor.isOn())
        return;
    motor.setMotorOn();
}
void stopEtc(etc::MotorController& motor) {
    if (!motor.isOn())
        return;
    motor.setMotorOff();
}

// ── ETC アーミング / SHUTDOWN 安全層 ──────────────────────────────────────
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
void armEtc(const TickDeps& d, EtcTarget::Mode ctrlMode, uint32_t now) {
    if (!d.plausibility.isValid(now)) {
        digitalWrite(SHUTDOWN_RELAY_PIN, LOW);  // ① 点火系を遮断 (恒久)
        stopEtc(d.motor);
    } else if (!d.hub.view().shutdownSig().isOn() || ctrlMode == EtcTarget::Mode::MotorOff) {
        stopEtc(d.motor);  // ② SIG_IN=LOW または ③ MOTOR_OFF → ETC 停止 (RELAY は HIGH のまま)
    } else if (d.experiment.wantsCoast()) {
        stopEtc(d.motor);  // ④ (①②③が常に優先)
    } else if (!d.flashGate.requested()) {
        startEtc(d.motor);  // SIG_IN=HIGH かつ違反なしかつ MOTOR_OFF でない → ETC 動作 (停止中なら再開)
    }
    // ⑤ 新しいパルスはシフター側で止め (suspend)、出ているパルスが終わってから止めて許可する
    if (d.flashGate.requested() && d.shifter.state() != shift::AutoShifter::State::Pulsing) {
        stopEtc(d.motor);
        d.flashGate.grant();
    }
}

}  // namespace

void tick(const TickDeps& d, uint32_t n) {
    const StopWatch elapsed(d.clock);
    const uint64_t nowUs64 = d.clock.nowUs64();  // この tick の中はこの時刻で統一する
    const uint32_t nowUs = (uint32_t)nowUs64;
    const uint32_t now = (uint32_t)(nowUs64 / 1000);

    // 1〜3 入力とモード
    d.hub.readShutdownSig(now);
    d.control.update(now);
    const EtcTarget::Mode ctrlMode = d.control.etcMode();
    d.hub.target().setMode(ctrlMode);  // MotorOff の実際の停止はアーミングで行う

    // 4〜5 プラウシビリティとアーミング → 6 モーター制御
    armEtc(d, ctrlMode, now);
    if (d.motor.isOn())
        d.motor.cycle(nowUs);

    // 7 同定実験。コーストの要求は次の tick のアーミングが拾う
    d.experiment.tick(now);

    // 8 シフター。flash 書き込み待ちの間は新しいパルスを出さない (止まった tick の中でパルスが伸びる)
    d.shifter.setSuspended(d.flashGate.requested());
    d.shifter.tick(
        {
            .autoOn = d.control.autoShiftOn(now),
            .upPressed = d.control.shiftUpPressed(now),
            .downPressed = d.control.shiftDownPressed(now),
        },
        nowUs);
    d.starter.tick(d.control.starterPressed(now), now);

    // 9 回転計測
    d.hub.updatePulse(nowUs64);

    // 10 CAN 送信。周期は kart-can の定義 (can.yaml の cycle_ms) どおり
    if (n % KART_CAN_DC_GYRO_XY_CYCLE_TIME_MS == 0)
        d.can.sendImu();
    // 0x604/0x605/0x607/0x608/0x609 (同じ 16 ms 周期)。0x600 系と半周期ずらして、1 回に書くフレーム数とバスの負荷をならす
    static_assert(KART_CAN_DC_PEDALS_CYCLE_TIME_MS == KART_CAN_DC_GYRO_XY_CYCLE_TIME_MS, "半周期ずらしは同じ周期が前提");
    if (n % KART_CAN_DC_PEDALS_CYCLE_TIME_MS == KART_CAN_DC_PEDALS_CYCLE_TIME_MS / 2)
        d.can.sendSensors();
    if (n % KART_CAN_DC_STATUS_CYCLE_TIME_MS == 0) {
        d.can.sendStatus({
            .etcMode = d.hub.view().target().getMode(),
            .launchActive = false,
            .autoShift = d.control.autoShiftOn(now),
            .starterRelay = d.starter.relayOn(),
            .shutdownLoopClosed = d.hub.view().shutdownSig().isOn(),
        });
    }

    // 11 SD ログの採取 (書くのは loop)
    d.logger.capture([&] { return buildLogRecord(nowUs64, now, d.logSources, d.stats.live()); });

    d.stats.noteTickUs(elapsed.elapsedUs());
}

}  // namespace app
