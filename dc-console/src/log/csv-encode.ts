// run (NNNN.bin の列) → CSV のテキスト。終端と欠けの判定は tools/decode_log.py と同じ:
// 全バイトがゼロのレコード、ファイルの半端、t_us の後退で止める。t_us の飛びは数えるだけ。
// Worker (csv.worker.ts) と Node の検証スクリプト (scripts/export_csv.ts) の両方から使う純粋なロジック。
import { COLUMNS, GAP_WARN_US, HEADER_SIZE, RECORD_SIZE, isZeroRecord, parseHeader, recordToRow, recordTUs } from "./log_record";

export interface CsvSink {
  rows(text: string): Promise<void>; // 行のまとまり (改行付き)。書き終わるまで待つと次の読みが止まる (メモリに溜めない)
  progress?(bytes: number, records: number): void;
  isCancelled?(): boolean;
}

export interface CsvResult {
  records: number;
  gaps: number;
  skipped: string[]; // header の無い空ファイル
  cancelled: boolean;
}

const CHUNK_RECORDS = Math.floor((8 << 20) / RECORD_SIZE); // ≈ 8 MB ずつ読む
const ROWS_PER_BATCH = 4000;

export async function encodeRun(files: Blob[], names: string[], decimate: number, sink: CsvSink): Promise<CsvResult> {
  let out: string[] = [COLUMNS.join(",")];
  let records = 0;
  let gaps = 0;
  let bytes = 0;
  let prevT: number | null = null;
  const skipped: string[] = [];
  for (let fi = 0; fi < files.length; fi++) {
    const file = files[fi];
    const h = parseHeader(await file.slice(0, HEADER_SIZE).arrayBuffer(), names[fi]);
    if (!h) { skipped.push(names[fi]); continue; }
    const n = Math.floor((file.size - HEADER_SIZE) / RECORD_SIZE);
    let stop = false;
    for (let start = 0; start < n && !stop; start += CHUNK_RECORDS) {
      if (sink.isCancelled?.()) return { records, gaps, skipped, cancelled: true };
      const count = Math.min(CHUNK_RECORDS, n - start);
      const off = HEADER_SIZE + start * RECORD_SIZE;
      const buf = await file.slice(off, off + count * RECORD_SIZE).arrayBuffer();
      const v = new DataView(buf);
      const raw = new Uint8Array(buf);
      for (let i = 0; i < count; i++) {
        const o = i * RECORD_SIZE;
        const t = recordTUs(v, o);
        if (t === 0 && isZeroRecord(raw, o)) { stop = true; break; } // 有効なデータの終わり
        if (prevT !== null) {
          if (t <= prevT) { stop = true; break; }
          if (t - prevT > GAP_WARN_US) gaps++;
        }
        prevT = t;
        if (records % decimate === 0) out.push(recordToRow(v, o, h).join(","));
        records++;
        bytes += RECORD_SIZE;
        if (out.length >= ROWS_PER_BATCH) { await sink.rows(out.join("\n") + "\n"); out = []; }
      }
      sink.progress?.(bytes, records);
    }
  }
  if (out.length) await sink.rows(out.join("\n") + "\n");
  return { records, gaps, skipped, cancelled: false };
}
