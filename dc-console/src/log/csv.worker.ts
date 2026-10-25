// CSV 書き出しの Worker。中身は csv-encode.ts。行は main に渡して書き終わりの ack を待つ。
import { encodeRun } from "./csv-encode";

export type CsvWorkerIn = { type: "start"; files: File[]; decimate: number } | { type: "ack" } | { type: "cancel" };
export type CsvWorkerOut =
  | { type: "rows"; text: string }
  | { type: "progress"; bytes: number; records: number }
  | { type: "done"; records: number; gaps: number; skipped: string[] }
  | { type: "error"; message: string };

let cancelled = false;
let ackResolve: (() => void) | null = null;

function post(m: CsvWorkerOut) {
  (self as unknown as Worker).postMessage(m);
}

self.onmessage = (e: MessageEvent<CsvWorkerIn>) => {
  const m = e.data;
  if (m.type === "start") {
    encodeRun(m.files, m.files.map((f) => f.name), m.decimate, {
      rows: (text) => { post({ type: "rows", text }); return new Promise((resolve) => { ackResolve = resolve; }); },
      progress: (bytes, records) => post({ type: "progress", bytes, records }),
      isCancelled: () => cancelled,
    })
      .then((r) => post(r.cancelled ? { type: "error", message: "cancelled" } : { type: "done", records: r.records, gaps: r.gaps, skipped: r.skipped }))
      .catch((err) => post({ type: "error", message: String((err as Error).message ?? err) }));
  } else if (m.type === "cancel") {
    cancelled = true;
  } else if (m.type === "ack") {
    ackResolve?.();
    ackResolve = null;
  }
};
