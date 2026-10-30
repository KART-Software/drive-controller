#include "isr/sensor_sampling_isr.hpp"

namespace {
const SensorSamplingDeps* deps = nullptr;
}  // namespace

void setSensorSamplingDeps(const SensorSamplingDeps& d) {
    deps = &d;
}

void sensorSamplingISR() {
    if (!deps)
        return;
    deps->hub.sampleDmaIsr();  // 前回の DMA 結果を反映して次を kick するだけ (数 µs)。ここで SPI を待たない
}
