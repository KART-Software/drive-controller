#include "isr/tick_isr.hpp"

namespace {
const TickDeps* deps = nullptr;
volatile uint32_t count = 0;
}  // namespace

void setTickDeps(const TickDeps& d) {
    deps = &d;
}

uint32_t tickCount() {
    return count;
}

void tickISR() {
    if (!deps)
        return;
    const uint32_t n = count + 1;
    count = n;
    app::tick(*deps, n);
}
