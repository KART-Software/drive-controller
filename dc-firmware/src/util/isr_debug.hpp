#pragma once

#include <Arduino.h>

// ISR (tickISR / canRxISR) から debug メッセージを出すためのキュー (docs/loop_nonblocking_spec.md §6.5)。
// ISR から Serial / sendDebugf を呼ぶと、loop の送信と USB の送信バッファを取り合って壊れる。
// ISR は文字列をここに積むだけにし、loop が取り出して sendDebugf する。
//
// - 書き手は優先度 64 の 2 つの ISR だけ (同じ優先度は互いに割り込まないので、書き手は実質 1 つ)。
//   読み手は loop だけ。
// - 書式は整数と文字列だけにする。newlib の浮動小数点の書式化は内部で malloc するので ISR では使えない。
//   小数が要る値は 10 倍して整数で出す。
// - 満杯なら捨てて数える (dropped)。
void isrDebugf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// loop から: たまっているメッセージを 1 つずつ fn に渡す
void drainIsrDebug(void (*fn)(const char* msg));
uint32_t isrDebugDropped();
