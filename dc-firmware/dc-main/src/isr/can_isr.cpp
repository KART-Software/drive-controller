#include "isr/can_isr.hpp"

namespace {
const CanRxDeps* deps = nullptr;
}  // namespace

void setCanRxDeps(const CanRxDeps& d) {
    deps = &d;
}

void canRxISR(const CanFrame& frame) {
    if (!deps)
        return;
    app::onCanRx(*deps, frame);
}
