#include "motor_controller.hpp"

namespace etc {

MotorController::MotorController(const EtcTarget& target, const Tps& tps) : target(target), tps(tps) {}

void MotorController::initialize() {
    dcMotor.initialize();
    // Anti-windup: 出力は ±DC_MOTOR_OUTPUT_SCALE_MAX で飽和するため、積分項の
    // 寄与をそれ以上積んでも巻き戻し遅れ (windup → オーバーシュート) にしか
    // ならない。積分寄与を出力フルスケールでクランプする。
    pid.setIntegralLimit(DC_MOTOR_OUTPUT_SCALE_MAX);
}

void MotorController::cycle() {
    double target_ = target.getTarget();
    double tp = tps.convertedValue();
    output = pid.compute(target_, tp);
    // DcMotor::write は |output| を DC_MOTOR_OUTPUT_SCALE_MAX で飽和させる。lastOutput_ は
    // 「実印加 duty」(SD ログ / 実験ガード / telemetry が参照) なので同じ飽和を掛ける。
    // 生 PID 出力は D 項で数万 % に達し得る (TPS 校正切替時に +169,000 % を記録した実績)。
    double applied = output;
    if (applied > DC_MOTOR_OUTPUT_SCALE_MAX)
        applied = DC_MOTOR_OUTPUT_SCALE_MAX;
    else if (applied < -DC_MOTOR_OUTPUT_SCALE_MAX)
        applied = -DC_MOTOR_OUTPUT_SCALE_MAX;
    // stopEtc() は setMotorOff() → timer.end() の順なので、その間に ISR が来ることがある。DcMotor::write は
    // OFF 中 no-op だが lastOutput_ は書かれてしまうため、OFF なら 0 (= 実印加値) にする。setMotorOff() は
    // dcMotor.off() → lastOutput_=0 の順なので、どの位置で割り込まれても最終値は 0 になる。
    lastOutput_ = dcMotor.isOn() ? (float)applied : 0.0f;
    dcMotor.write(output);
}

void MotorController::setMotorOn() {
    pid.reset();
    dcMotor.on();
}

void MotorController::setMotorOff() {
    dcMotor.off();
    lastOutput_ = 0.0f;  // 停止中の凍結値を残さない (duty ログ = 常に実印加値)
}

bool MotorController::isOn() const {
    return dcMotor.isOn();
}

void MotorController::setPidGains(double kP, double kI, double kD) {
    // モーター ISR (優先度 0) が compute 中にゲイン 3 値の書き換えへ割り込むと
    // 混成ゲイン/破損 double を読み得るため、書き換えはアトミックに行う。
    noInterrupts();
    pid.setGains(kP, kI, kD);
    interrupts();
}

}  // namespace etc