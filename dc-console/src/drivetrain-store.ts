/**
 * DrivetrainStore — 軽量な in-memory リングバッファ。
 * 非 ETC のドライブトレイン時系列 (車輪速 / エンジン RPM) をライブチャート用に保持する。
 * ETC の sensor-store (IndexedDB 永続) とは独立。永続化はしない (直近のみ)。
 *
 * 列: [ts, fl, fr, rl, rr, rpm]
 */
import type { SensorData } from "./types";

const MEM_LIMIT = 6000; // ≈2 min @ 50Hz
const NUM_COLS = 6;

export class DrivetrainStore {
  mem: number[][] = Array.from({ length: NUM_COLS }, () => []);
  latest: SensorData | null = null;

  get memRows(): number {
    return this.mem[0].length;
  }

  reset(): void {
    this.mem = Array.from({ length: NUM_COLS }, () => []);
    this.latest = null;
  }

  push(d: SensorData): void {
    this.latest = d;
    const vals = [
      d.ts / 1000,
      d.wheelFL ?? 0,
      d.wheelFR ?? 0,
      d.wheelRL ?? 0,
      d.wheelRR ?? 0,
      d.rpm ?? 0,
    ];
    for (let c = 0; c < NUM_COLS; c++) this.mem[c].push(vals[c]);
    if (this.mem[0].length > MEM_LIMIT) {
      const excess = this.mem[0].length - MEM_LIMIT;
      for (let c = 0; c < NUM_COLS; c++) this.mem[c] = this.mem[c].slice(excess);
    }
  }
}

export const drivetrainStore = new DrivetrainStore();

/** uPlot 列インデックス */
export const DT_COL = { ts: 0, fl: 1, fr: 2, rl: 3, rr: 4, rpm: 5 } as const;
