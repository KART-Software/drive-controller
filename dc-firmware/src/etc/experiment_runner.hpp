#pragma once

#include <Arduino.h>

#include "control/control_input.hpp"
#include "etc/motor_controller.hpp"
#include "etc/plausibility_validator.hpp"
#include "sensor/sensor_hub.hpp"
#include "util/log/sensor_logger.hpp"

namespace etc {

// ETC 同定実験モード (docs/etc_experiment_mode_spec.md)。
// loop() から毎イテレーション tick() される FSM。目標値は EtcTarget の manual
// モード経由で注入するので、PID・モーター経路は通常運用と完全に同一
// (「使う構成のまま測る」という同定の前提)。
//
// リリース試験のコーストは自分でモーターを切らず wantsCoast() を立てるだけ。
// 実際の停止/再開は main.cpp の ETC アーミング層 (SHUTDOWN 安全層) が行う —
// ①プラウシビリティ ②SIG_IN ③MOTOR_OFF が常にコーストより優先される。
//
// フェーズ遷移の共通部品は「静止判定」(spec Q1/Q4/Q7):
//   直近 EXPERIMENT_STATIONARY_WINDOW_MS の窓で |Δtps| と |Δduty| が閾値未満。
//   タイムアウト (8s) 時は黙って次フェーズへ進み、件数のみ完了時に報告 (Q3)。
class ExperimentRunner {
   public:
    enum class Type : uint8_t { None = 0, PointDwell = 1, Release = 2, Step = 3 };
    enum class Phase : uint8_t { Idle = 0, Settling = 1, Holding = 2, Coasting = 3 };

    ExperimentRunner(const SensorHub& hub,
                     const ControlInput& control,
                     EtcTarget& target,
                     const MotorController& motor,
                     PlausibilityValidator& validator,
                     const SensorLogger& logger);

    bool start(Type type, unsigned long now);  // 前提条件 (§5) を満たさなければ false
    void stop();            // 完走/中断共通の後始末 (manual 解除・チェックフラグ復元)
    void tick(unsigned long now);

    bool active() const { return type_ != Type::None; }
    // リリース試験のコースト区間。main の ETC アーミングが参照して stopEtc() する。
    bool wantsCoast() const { return active() && phase_ == Phase::Coasting; }

    // SD ログマーカー (LogRecord.exp_*)
    uint8_t logType() const { return (uint8_t)type_; }
    uint8_t logIndex() const { return (uint8_t)index_; }
    uint8_t logPhase() const { return (uint8_t)phase_; }

   private:
    bool preconditionsOk() const;
    bool guardsOk(unsigned long now);  // 破れたら理由を debug 送信して false
    double currentTargetTp() const;
    uint16_t totalSteps() const;
    void advance(unsigned long now);
    void enterPhase(Phase phase, unsigned long now);
    bool stationaryOrTimeout(unsigned long now);  // 静止判定 + タイムアウト (共通部品)

    const SensorHub& hub_;
    const ControlInput& control_;
    EtcTarget& target_;
    const MotorController& motor_;
    PlausibilityValidator& validator_;
    const SensorLogger& logger_;

    Type type_ = Type::None;
    Phase phase_ = Phase::Idle;
    uint16_t index_ = 0;     // シーケンス内インデックス (ログの exp_index)
    bool stepPost_ = false;  // Step: false=開始位置へ整定中 / true=到達位置側
    unsigned long phaseStartMs_ = 0;
    uint16_t timeoutCount_ = 0;  // 静止タイムアウト件数 (完了時に報告, Q3)

    // 静止判定窓 (tumbling window: 閾値を破ったら窓を張り直す)
    unsigned long stationaryWindowStartMs_ = 0;
    float tpMin_ = 0.0f, tpMax_ = 0.0f;
    float dutyMin_ = 0.0f, dutyMax_ = 0.0f;
    void resetStationaryWindow(unsigned long now);

    // ストールガード (|u| 大 + 目標乖離 + 不動の継続)
    unsigned long stallSinceMs_ = 0;
    float stallRefTp_ = 0.0f;

};

}  // namespace etc
