#pragma once

#include "can/can_controller.hpp"
#include "interfaces/can/can_frame.hpp"
#include "interfaces/util/clock.hpp"
#include "shift/auto_shifter.hpp"

// CAN 受信のロジックが使う部品 (docs/loop_nonblocking_spec.md §3.3)。main.cpp の Parts が持ち、isr/can_isr が受け取って渡す
struct CanRxDeps {
    const Clock& clock;
    CanController& can;
    shift::AutoShifter& shifter;
};

namespace app {

// FlexCAN3 の受信割り込み (優先度 64 = tick と同じなので互いに割り込まない) から 1 フレームごと (§6.3)。
// 即時に処理すべき入力 (0x741 のシフトパドル) は受けたその場でシフターへ渡す (tick を待たない)
void onCanRx(const CanRxDeps& d, const CanFrame& frame);

}  // namespace app
