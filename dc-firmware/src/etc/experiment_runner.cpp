#include "experiment_runner.hpp"

#include "constants.hpp"
#include "serial/serial_protocol.hpp"

namespace etc {

namespace {

// シーケンス定義 (docs/etc_experiment_mode_spec.md §3):
//   PointDwell: -5..105% を 1% 刻み 111 点で上り → 104..-5% を 1% 刻みで下り (計 221)
//   Release:    -5..105% を 1% 刻み 111 点。各点で保持 → コースト (バネ戻り)
//   Step:       0..100% の 10% 刻み 11 点 × 11 点から from==to を除いた 110 ペア
constexpr uint16_t DWELL_STEPS = 221;
constexpr uint16_t RELEASE_STEPS = 111;
constexpr uint16_t STEP_STEPS = 110;

double dwellTp(uint16_t i) {
    return (i <= 110) ? (-5.0 + i) : (105.0 - (double)(i - 110));
}

double releaseTp(uint16_t i) {
    return -5.0 + i;
}

// step ペア列: from 昇順 → to 昇順、from==to は除外して詰める (exp_index 0-109)。
// k = 10i + j' (j' = 0..9), j = j' >= i ? j'+1 : j' で対角を飛ばす。
double stepFromTp(uint16_t k) {
    return (double)(k / 10) * 10.0;
}

double stepToTp(uint16_t k) {
    uint16_t i = k / 10;
    uint16_t j = k % 10;
    if (j >= i)
        j++;
    return (double)j * 10.0;
}

}  // namespace

ExperimentRunner::ExperimentRunner(const SensorHub& hub,
                                   const ControlInput& control,
                                   EtcTarget& target,
                                   const MotorController& motor,
                                   PlausibilityValidator& validator,
                                   const SensorLogger& logger)
    : hub_(hub), control_(control), target_(target), motor_(motor), validator_(validator), logger_(logger) {}

uint16_t ExperimentRunner::totalSteps() const {
    switch (type_) {
        case Type::PointDwell:
            return DWELL_STEPS;
        case Type::Release:
            return RELEASE_STEPS;
        case Type::Step:
            return STEP_STEPS;
        default:
            return 0;
    }
}

double ExperimentRunner::currentTargetTp() const {
    switch (type_) {
        case Type::PointDwell:
            return dwellTp(index_);
        case Type::Release:
            return releaseTp(index_);
        case Type::Step:
            return stepPost_ ? stepToTp(index_) : stepFromTp(index_);
        default:
            return 0.0;
    }
}

bool ExperimentRunner::preconditionsOk() const {
    if (active())
        return false;  // 二重開始拒否
    // MOTOR_OFF はアーミング③が ETC を止めるため実験が進行できない (spec Q11)。
    if (control_.etcMode() == EtcTarget::Mode::MotorOff)
        return false;
    if (hub_.pulseEngine().getFrequencyHz() > 0.5f)  // エンジン停止
        return false;
    if (hub_.pulseWheelFL().getFrequencyHz() > 0.5f || hub_.pulseWheelFR().getFrequencyHz() > 0.5f ||
        hub_.pulseWheelRL().getFrequencyHz() > 0.5f || hub_.pulseWheelRR().getFrequencyHz() > 0.5f)
        return false;  // 車両静止
    if (!validator_.currentlyValid() || !validator_.validLatched())
        return false;
    // ETC が実際に稼働していること: ② SIG_IN=HIGH かつモーター ON。止まったまま開始すると
    // TPS が休止位置で「静止」と判定され、無意味なデータで完走してしまう
    if (!hub_.shutdownSig().isOn() || !motor_.isOn())
        return false;
    if (!logger_.active())  // SD 無しでは記録できない実験は無意味 (spec Q13)
        return false;
    return true;
}

bool ExperimentRunner::start(Type type, unsigned long now) {
    if (type == Type::None || !preconditionsOk())
        return false;
    // target-vs-TPS チェックはコースト/大ステップで必ず乖離するため一時無効化。
    // 回路チェック (断線検出) は有効のまま。stop() で復元する。
    validator_.suspendTargetCheck(true);
    if (!target_.isManual())
        target_.setManual();
    type_ = type;
    index_ = 0;
    stepPost_ = false;
    timeoutCount_ = 0;
    stallSinceMs_ = 0;
    target_.setManualTarget(currentTargetTp());
    enterPhase(Phase::Settling, now);
    SerialProtocol::sendDebugf("EXP start type=%u steps=%u", (unsigned)type_, totalSteps());
    return true;
}

void ExperimentRunner::stop() {
    if (!active()) {
        // 非実行中でも EXP stop を返す: 実験中に再起動 / USB 断があると console は EXP stop を受け取れず
        // 「実行中」表示が張り付く。停止ボタンでそれを解除できるようにする
        SerialProtocol::sendDebugf("EXP stop (not running)");
        return;
    }
    if (target_.isManual())
        target_.setManual();  // manual 解除 → 通常のモード別ターゲットへ復帰
    validator_.suspendTargetCheck(false);
    type_ = Type::None;
    phase_ = Phase::Idle;
    SerialProtocol::sendDebugf("EXP stop");
}

void ExperimentRunner::resetStationaryWindow(unsigned long now) {
    stationaryWindowStartMs_ = now;
    float tp = (float)hub_.tps1().convertedValue();
    float duty = motor_.lastOutput();
    tpMin_ = tpMax_ = tp;
    dutyMin_ = dutyMax_ = duty;
}

void ExperimentRunner::enterPhase(Phase phase, unsigned long now) {
    phase_ = phase;
    phaseStartMs_ = now;
    resetStationaryWindow(now);
}

// 静止判定 (spec Q1/Q4/Q7 共通部品): 窓内の tps / duty の変動幅が閾値未満のまま
// EXPERIMENT_STATIONARY_WINDOW_MS 継続したら静止。閾値を破ったら窓を張り直す。
// EXPERIMENT_SETTLE_TIMEOUT_MS 経過で件数を数えて true (黙って続行, Q3)。
bool ExperimentRunner::stationaryOrTimeout(unsigned long now) {
    float tp = (float)hub_.tps1().convertedValue();
    float duty = motor_.lastOutput();
    if (tp < tpMin_)
        tpMin_ = tp;
    if (tp > tpMax_)
        tpMax_ = tp;
    if (duty < dutyMin_)
        dutyMin_ = duty;
    if (duty > dutyMax_)
        dutyMax_ = duty;

    if ((tpMax_ - tpMin_) > (float)EXPERIMENT_STATIONARY_TP_TOL ||
        (dutyMax_ - dutyMin_) > (float)EXPERIMENT_STATIONARY_DUTY_TOL) {
        resetStationaryWindow(now);
    } else if (now - stationaryWindowStartMs_ >= EXPERIMENT_STATIONARY_WINDOW_MS) {
        return true;
    }

    if (now - phaseStartMs_ >= EXPERIMENT_SETTLE_TIMEOUT_MS) {
        timeoutCount_++;
        return true;
    }
    return false;
}

void ExperimentRunner::advance(unsigned long now) {
    index_++;
    stepPost_ = false;
    if (index_ >= totalSteps()) {
        SerialProtocol::sendDebugf("EXP done type=%u timeouts=%u", (unsigned)type_, timeoutCount_);
        stop();
        return;
    }
    if ((index_ % 10) == 0)
        SerialProtocol::sendDebugf("EXP progress %u/%u", index_, totalSteps());
    target_.setManualTarget(currentTargetTp());
    enterPhase(Phase::Settling, now);
}

bool ExperimentRunner::guardsOk(unsigned long now) {
    if (control_.etcMode() == EtcTarget::Mode::MotorOff) {
        SerialProtocol::sendDebugf("EXP abort: MOTOR_OFF knob");
        return false;
    }
    if (hub_.pulseEngine().getFrequencyHz() > 0.5f) {
        SerialProtocol::sendDebugf("EXP abort: engine running");
        return false;
    }
    if (!validator_.currentlyValid() || !validator_.validLatched()) {
        SerialProtocol::sendDebugf("EXP abort: plausibility");
        return false;
    }
    if (!hub_.shutdownSig().isOn()) {
        SerialProtocol::sendDebugf("EXP abort: SIG_IN low");
        return false;
    }
    // コースト区間はモーターを意図的に止めている。それ以外で OFF なら ETC が止まっている
    if (phase_ != Phase::Coasting && !motor_.isOn()) {
        SerialProtocol::sendDebugf("EXP abort: ETC not armed");
        return false;
    }
    double tp = hub_.tps1().convertedValue();
    if (tp > EXPERIMENT_TP_GUARD_HIGH) {
        SerialProtocol::sendDebugf("EXP abort: tps high %.1f", tp);
        return false;
    }
    // 低側ガード: リリース試験はコースト~再アームでバネ休止位置 (-15% 付近) を
    // 通るのが正常挙動なので適用しない (モーターは切れており押し付けは起きない)。
    if (type_ != Type::Release && tp < EXPERIMENT_TP_GUARD_LOW) {
        SerialProtocol::sendDebugf("EXP abort: tps low %.1f", tp);
        return false;
    }
    // ストール: PID がほぼ飽和 + 目標乖離 + 不動の継続 (固着/想定外の押し付け)。
    // 「目標付近で高 duty 保持」は高開度の正常動作なので目標乖離条件を必ず含める。
    float u = motor_.lastOutput();
    bool stallCandidate = phase_ != Phase::Coasting && fabsf(u) > (float)EXPERIMENT_STALL_DUTY &&
                          fabs(tp - currentTargetTp()) > EXPERIMENT_STALL_TARGET_DEV;
    if (stallCandidate) {
        if (stallSinceMs_ == 0) {
            stallSinceMs_ = now;
            stallRefTp_ = (float)tp;
        } else if (now - stallSinceMs_ >= EXPERIMENT_STALL_MS) {
            if (fabsf((float)tp - stallRefTp_) < 0.5f) {
                SerialProtocol::sendDebugf("EXP abort: stall u=%.0f tp=%.1f", (double)u, tp);
                return false;
            }
            stallSinceMs_ = now;  // 動いてはいる → 監視窓を更新して継続
            stallRefTp_ = (float)tp;
        }
    } else {
        stallSinceMs_ = 0;
    }
    return true;
}

void ExperimentRunner::tick(unsigned long now) {
    if (!active())
        return;
    if (!guardsOk(now)) {
        stop();
        return;
    }
    switch (phase_) {
        case Phase::Settling:
            if (!stationaryOrTimeout(now))
                break;
            if (type_ == Type::Step) {
                if (!stepPost_) {
                    stepPost_ = true;  // 開始位置で静止 → 到達位置へステップ
                    target_.setManualTarget(currentTargetTp());
                    enterPhase(Phase::Settling, now);
                } else {
                    advance(now);  // 到達位置で静止 → 次ペア (固定保持なし, Q7)
                }
            } else {
                enterPhase(Phase::Holding, now);  // Dwell: 記録窓 / Release: リリース前保持
            }
            break;
        case Phase::Holding: {
            unsigned long holdMs =
                (type_ == Type::PointDwell) ? EXPERIMENT_RECORD_HOLD_MS : EXPERIMENT_RELEASE_HOLD_MS;
            if (now - phaseStartMs_ < holdMs)
                break;
            if (type_ == Type::Release) {
                enterPhase(Phase::Coasting, now);  // main のアーミング層が stopEtc() する
            } else {
                advance(now);
            }
            break;
        }
        case Phase::Coasting:
            // バネ戻りの記録期間。戻りが静止したら次の点へ (Q4: 静止判定の再利用。
            // コースト中は duty=0 なので実質 θ の不動判定)。再アームはアーミング層が
            // 行い、startEtc() 内の pid.reset() で積分も仕切り直される。
            if (stationaryOrTimeout(now))
                advance(now);
            break;
        default:
            break;
    }
}

}  // namespace etc
