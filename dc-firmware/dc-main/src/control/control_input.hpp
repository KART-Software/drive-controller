#pragma once

#include <Arduino.h>

#include <optional>

#include "can/can_data.hpp"
#include "sensor/sensors.hpp"

// ドライバー指令 (コックピットスイッチ) の唯一の入口 (docs/control_input_spec.md)。
// 入力元は CAN (data_logger → 0x740 Control / 0x741 Shift) と、ベンチ用の console override。
// 消費側 (main のモード反映・アーミング③、AutoShifter、ExperimentRunner、ログ、telemetry) は
// このクラスだけを見て、入力が CAN から来たのか override なのかを知らない。安全規則 (未受信時の待ち、途絶時の
// フォールバック、MotorOff のラッチ) はすべてここにあり、CanRxData は受信した値と時刻を持つだけ。
//
// update() は tickISR から 1 ms ごと。時刻で判定する getter (途絶・経過時間) は呼ぶ側が時刻を渡す。
// 受信時刻 (CanRxData) は CAN 受信割り込みがその場の時刻で刻むので、渡された時刻より新しいことがある
// (loop が時刻を読んだ後に受信した場合など)。その差は符号付きで扱い、新しいものは経過 0 とみなす。
// setOverride / clearOverride は loop (コマンド) から呼ばれ、中で割り込みを禁止して書き換える。
class ControlInput {
   public:
    enum class Source : uint8_t { WaitingForCan, Can, Override };

    // console からの上書き (RAM のみ、再起動で解除)。未指定の項目は、上書き中なら今の上書き値を保ち、
    // そうでなければ CAN 側の値を使う
    struct Override {
        bool hasEtcMode;
        EtcTarget::Mode etcMode;
        bool hasAutoShift;
        bool autoShift;
    };

    explicit ControlInput(const CanRxData& rx) : rx_(rx) {}

    void update(uint32_t nowMs);  // tickISR から毎回

    // 0x740 を受信している間は受け付けない (車載時にドライバーのスイッチを上書きしないため)
    bool setOverride(const Override& o, uint32_t nowMs);
    void clearOverride();

    EtcTarget::Mode etcMode() const;
    bool autoShiftOn(uint32_t nowMs) const;
    bool launchOn(uint32_t nowMs) const { return controlLinkAlive(nowMs) && rx_.launchActive; }  // launch は凍結中
    bool shiftUpPressed(uint32_t nowMs) const { return shiftLinkAlive(nowMs) && rx_.shiftUp; }
    bool shiftDownPressed(uint32_t nowMs) const { return shiftLinkAlive(nowMs) && rx_.shiftDown; }
    bool starterPressed(uint32_t nowMs) const { return controlLinkAlive(nowMs) && rx_.starter; }

    // CAN 側の値 (override を適用する前。telemetry 表示用)
    std::optional<EtcTarget::Mode> canEtcMode() const;  // 未受信は nullopt
    bool canAutoShift(uint32_t nowMs) const { return controlLinkAlive(nowMs) && rx_.autoShiftActive; }

    Source source() const;
    bool controlLinkAlive(uint32_t nowMs) const;
    bool shiftLinkAlive(uint32_t nowMs) const;
    uint32_t controlRxAgeMs(uint32_t nowMs) const;  // 未受信は 0

   private:
    bool setOverrideLocked(const Override& o, uint32_t nowMs);

    const CanRxData& rx_;
    uint32_t seenControlFrames_ = 0;
    EtcTarget::Mode canMode_ = EtcTarget::Mode::Normal;  // 取り込み済みの CAN のモード (フォールバック適用後)
    volatile bool overrideActive_ = false;
    Override override_ = {
        .hasEtcMode = false, .etcMode = EtcTarget::Mode::MotorOff, .hasAutoShift = false, .autoShift = false};
};
