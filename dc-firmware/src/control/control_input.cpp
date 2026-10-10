#include "control_input.hpp"

#include "constants.hpp"
#include "util/log/debug_logger.hpp"

namespace {

// 受信時刻からの経過。受信時刻は CAN poll 中の millis() なので、loop 先頭で取った nowMs より
// 後になることがある。その場合は 0 とする (符号なしの引き算で巨大値になり「途絶」と誤判定しないため)
uint32_t ageMs(unsigned long nowMs, unsigned long rxMs) {
    const int32_t d = (int32_t)(nowMs - rxMs);
    return d > 0 ? (uint32_t)d : 0;
}

}  // namespace

void ControlInput::update(unsigned long nowMs) {
    nowMs_ = nowMs;
    // 新しい 0x740 のモードを取り込む。UNSPECIFIED や未知値 (nullopt) はモードを変えない
    if (rx_.controlFrames != seenControlFrames_) {
        seenControlFrames_ = rx_.controlFrames;
        if (rx_.etcMode)
            canMode_ = *rx_.etcMode;
    }
    // 途絶: MotorOff は安全ラッチとして保持 (CAN 断で勝手にモーターを復帰させない)。それ以外は Normal
    if (rx_.controlFrames > 0 && !controlLinkAlive() && canMode_ != EtcTarget::Mode::MotorOff)
        canMode_ = EtcTarget::Mode::Normal;
    if (overrideActive_ && controlLinkAlive()) {
        overrideActive_ = false;
        DebugLogger::log("control: override cleared (CAN 0x740 received)");
    }
}

bool ControlInput::setOverride(const Override& o) {
    if (controlLinkAlive())
        return false;
    // 上書き中なら未指定の項目は今の上書き値を保つ (オートシフトだけ切り替えてもモードの上書きが消えない)
    if (overrideActive_) {
        if (o.hasEtcMode) {
            override_.hasEtcMode = true;
            override_.etcMode = o.etcMode;
        }
        if (o.hasAutoShift) {
            override_.hasAutoShift = true;
            override_.autoShift = o.autoShift;
        }
    } else {
        override_ = o;
    }
    overrideActive_ = true;
    return true;
}

EtcTarget::Mode ControlInput::etcMode() const {
    if (overrideActive_ && override_.hasEtcMode)
        return override_.etcMode;
    if (rx_.controlFrames == 0)
        return EtcTarget::Mode::MotorOff;  // 送り手 (data_logger) を待つ間は ETC を動かさない
    return canMode_;
}

bool ControlInput::autoShiftOn() const {
    if (overrideActive_ && override_.hasAutoShift)
        return override_.autoShift;
    return canAutoShift();  // 未受信・途絶時は false (手動)
}

std::optional<EtcTarget::Mode> ControlInput::canEtcMode() const {
    if (rx_.controlFrames == 0)
        return std::nullopt;
    return canMode_;
}

ControlInput::Source ControlInput::source() const {
    if (overrideActive_)
        return Source::Override;
    return rx_.controlFrames > 0 ? Source::Can : Source::WaitingForCan;
}

bool ControlInput::controlLinkAlive() const {
    return rx_.controlFrames > 0 && ageMs(nowMs_, rx_.lastControlFrameMs) <= CAN_CONTROL_TIMEOUT_MS;
}

bool ControlInput::shiftLinkAlive() const {
    return rx_.shiftFrames > 0 && ageMs(nowMs_, rx_.lastShiftFrameMs) <= CAN_SHIFT_TIMEOUT_MS;
}

uint32_t ControlInput::controlRxAgeMs() const {
    return rx_.controlFrames > 0 ? ageMs(nowMs_, rx_.lastControlFrameMs) : 0;
}
