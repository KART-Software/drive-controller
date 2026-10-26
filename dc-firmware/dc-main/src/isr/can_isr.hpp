#pragma once

#include "app/can.hpp"

// FlexCAN3 の受信割り込みの入口 (docs/loop_nonblocking_spec.md §3.3)。依存を app::onCanRx に渡すだけ
void setCanRxDeps(const CanRxDeps& deps);  // setup の先頭で 1 回。deps は static 寿命であること
void canRxISR(const CanFrame& frame);      // CanController::begin に渡す
