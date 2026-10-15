#pragma once

#include <Arduino.h>

#include "control/control_input.hpp"
#include "interfaces/util/clock.hpp"
#include "etc/motor_controller.hpp"
#include "etc/plausibility_validator.hpp"
#include "sensor/sensor_hub.hpp"
#include "util/log/sensor_logger.hpp"

namespace etc {

// ETC 同定実験モード (docs/etc_experiment_mode_spec.md)。
// tickISR から 1 ms ごとに tick() される FSM。目標値は EtcTarget の manual
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
                     const SensorLogger& logger,
                     const Clock& clock);  // clock: loop から要求の処理を待つときのタイムアウト

    // loop (コマンド) から。状態を書き換えるのは tick だけにするため、要求を置いて tick の処理を待つ
    // (1〜2 ms)。start は前提条件 (§5) を満たさなければ false
    bool requestStart(Type type);
    void requestStop();

    // tickISR から。開始 / 停止の要求を取り込んでから FSM を進める
    void tick(unsigned long now);

    bool active() const { return type_ != Type::None; }
    // リリース試験のコースト区間。main の ETC アーミングが参照して stopEtc() する。
    bool wantsCoast() const { return active() && phase_ == Phase::Coasting; }

    // SD ログマーカー (LogRecord.exp_*)
    uint8_t logType() const { return (uint8_t)type_; }
    uint8_t logIndex() const { return (uint8_t)index_; }
    uint8_t logPhase() const { return (uint8_t)phase_; }

   private:
    bool start(Type type, unsigned long now);
    void stop(unsigned long now);  // 完走/中断共通の後始末 (manual 解除・チェックフラグ復元)
    // loop → tick の要求。loop が req_ を立てて待ち、tick が処理して None に戻す
    enum class Request : uint8_t { None, Start, Stop };
    volatile Request req_ = Request::None;
    volatile Type reqType_ = Type::None;
    volatile bool reqResult_ = false;
    bool waitRequest(Request r);

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
    const Clock& clock_;

    volatile Type type_ = Type::None;
    volatile Phase phase_ = Phase::Idle;
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
