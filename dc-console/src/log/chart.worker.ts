// チャートの Worker: run を開いて概観を持ち、表示範囲のクエリに答える (docs/log_viewer_spec.md §6.2)。1 run につき 1 つ。
import { buildOverview, queryRange, type Gap, type Overview } from "./overview";

export interface OverviewMeta {
  t0Us: number;
  t1Us: number;
  bucketUs: number;
  n: number;
  gaps: Gap[];
  rtcEpoch: number;
  rtcTUs: number;
}

export type ChartWorkerIn =
  | { type: "open"; files: File[] }
  | { type: "query"; gen: number; t0Us: number; t1Us: number; width: number; channels: number[] };
export type ChartWorkerOut =
  | { type: "progress"; bytes: number; total: number }
  | { type: "ready"; meta: OverviewMeta }
  | { type: "data"; gen: number; channels: number[]; t: Float64Array; min: Float32Array[]; max: Float32Array[]; raw: boolean }
  | { type: "error"; message: string };

let files: File[] = [];
let ov: Overview | null = null;

function post(m: ChartWorkerOut, transfer: Transferable[] = []) {
  (self as unknown as Worker).postMessage(m, transfer);
}

self.onmessage = (e: MessageEvent<ChartWorkerIn>) => {
  const m = e.data;
  if (m.type === "open") {
    files = m.files;
    ov = null;
    buildOverview(files, (bytes, total) => post({ type: "progress", bytes, total }))
      .then((o) => {
        ov = o;
        post({ type: "ready", meta: { t0Us: o.t0Us, t1Us: o.t1Us, bucketUs: o.bucketUs, n: o.n, gaps: o.gaps, rtcEpoch: o.rtcEpoch, rtcTUs: o.rtcTUs } });
      })
      .catch((err) => post({ type: "error", message: String((err as Error).message ?? err) }));
  } else if (m.type === "query") {
    if (!ov) return;
    queryRange(files, ov, m.t0Us, m.t1Us, m.width, m.channels)
      .then((d) => post({ type: "data", gen: m.gen, channels: m.channels, ...d }, [d.t.buffer, ...d.min.map((a) => a.buffer), ...d.max.map((a) => a.buffer)]))
      .catch((err) => post({ type: "error", message: String((err as Error).message ?? err) }));
  }
};
