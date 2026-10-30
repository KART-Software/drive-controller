#pragma once

#include <stdint.h>

#include "app/tick.hpp"

// GPT2 の 1 ms 割り込みの入口 (docs/loop_nonblocking_spec.md §3.3)。依存を app::tick に渡すだけで、判断は持たない
void setTickDeps(const TickDeps& deps);  // setup の先頭で 1 回。deps は static 寿命であること
void tickISR();                          // Gpt2Timer::begin に渡す
uint32_t tickCount();                    // 何回呼ばれたか (bench の debug_stall 用)
