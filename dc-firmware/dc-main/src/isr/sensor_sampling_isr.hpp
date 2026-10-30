#pragma once

#include "sensor/sensor_hub.hpp"

// センサーサンプリング (PIT、16 kHz) の割り込みの入口 (docs/loop_nonblocking_spec.md §3.3)。
// ロジックは SensorHub::sampleDmaIsr() そのものなので app/ 側のファイルは持たず、ここから直接呼ぶ
struct SensorSamplingDeps {
    SensorHub& hub;
};

void setSensorSamplingDeps(const SensorSamplingDeps& deps);  // setup の先頭で 1 回。deps は static 寿命であること
void sensorSamplingISR();                                    // IntervalTimer::begin に渡す
