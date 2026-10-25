// チャート用のデータ (docs/log_viewer_spec.md §6.2): run を 1 回走査した概観 (バケットごとの min / max) と、表示範囲のクエリ。
// Worker (chart.worker.ts) と Node の検証 (scripts/check_chart.mts) の両方から使う純粋なロジック。
import { CHANNELS, readChannel } from "./channels";
import { findLastRecord, readHeader, tUsAt } from "./decoder";
import { GAP_WARN_US, HEADER_SIZE, RECORD_SIZE, recordTUs } from "./log_record";

export const OVERVIEW_BUCKETS = 20000;
const CHUNK_RECORDS = Math.floor((8 << 20) / RECORD_SIZE);
const MAX_FILE_QUERY_RECORDS = 300000; // これより広い範囲はファイルを読み直さず概観で済ませる (≈ 53 MB)

export interface FileIndex {
  file: number; // files[] の番号
  firstTUs: number;
  lastTUs: number;
  records: number; // 有効なレコード数
}

export interface Gap {
  t0Us: number;
  t1Us: number;
}

export interface Overview {
  t0Us: number;
  t1Us: number;
  bucketUs: number;
  n: number;
  min: Float32Array[]; // [channel][bucket]。データの無いバケットは +Infinity
  max: Float32Array[]; // 同、-Infinity
  gaps: Gap[];
  rtcEpoch: number;
  rtcTUs: number;
  files: FileIndex[];
}

export interface RangeData {
  t: Float64Array; // t_us (µs)。生なら各レコード、バケットならその中心
  min: Float32Array[]; // [要求したチャネルの順][点]。データ無しは NaN
  max: Float32Array[];
  raw: boolean; // 生の 1 kHz の値 (min = max)
}

async function indexFiles(files: Blob[]) {
  const index: FileIndex[] = [];
  let rtcEpoch = 0;
  let rtcTUs = 0;
  for (let i = 0; i < files.length; i++) {
    const h = await readHeader(files[i] as File);
    if (!h) continue;
    if (!rtcEpoch && h.rtcEpoch) { rtcEpoch = h.rtcEpoch; rtcTUs = h.rtcTUs; }
    const last = await findLastRecord(files[i] as File);
    if (last < 0) continue;
    index.push({ file: i, firstTUs: await tUsAt(files[i] as File, 0), lastTUs: await tUsAt(files[i] as File, last), records: last + 1 });
  }
  return { index, rtcEpoch, rtcTUs };
}

/** ファイル内で t_us >= t となる最初のレコード番号 (無ければ records)。有効な範囲では t_us は単調増加 */
async function lowerBound(file: Blob, records: number, t: number): Promise<number> {
  let lo = 0;
  let hi = records;
  while (lo < hi) {
    const mid = (lo + hi) >> 1;
    if ((await tUsAt(file as File, mid)) < t) lo = mid + 1;
    else hi = mid;
  }
  return lo;
}

export async function buildOverview(files: Blob[], onProgress?: (bytes: number, total: number) => void): Promise<Overview> {
  const { index, rtcEpoch, rtcTUs } = await indexFiles(files);
  if (index.length === 0) throw new Error("有効なレコードがありません");
  const t0Us = index[0].firstTUs;
  const t1Us = index[index.length - 1].lastTUs;
  const bucketUs = Math.max(1000, Math.ceil((t1Us - t0Us + 1) / OVERVIEW_BUCKETS));
  const n = Math.floor((t1Us - t0Us) / bucketUs) + 1;
  const min = CHANNELS.map(() => new Float32Array(n).fill(Infinity));
  const max = CHANNELS.map(() => new Float32Array(n).fill(-Infinity));
  const gaps: Gap[] = [];
  const total = index.reduce((s, f) => s + f.records * RECORD_SIZE, 0);
  let bytes = 0;
  let prevT: number | null = null;
  for (const fi of index) {
    const file = files[fi.file];
    let stop = false;
    for (let start = 0; start < fi.records && !stop; start += CHUNK_RECORDS) {
      const count = Math.min(CHUNK_RECORDS, fi.records - start);
      const off = HEADER_SIZE + start * RECORD_SIZE;
      const v = new DataView(await file.slice(off, off + count * RECORD_SIZE).arrayBuffer());
      for (let i = 0; i < count; i++) {
        const o = i * RECORD_SIZE;
        const t = recordTUs(v, o);
        if (prevT !== null) {
          if (t <= prevT) { stop = true; break; }
          if (t - prevT > GAP_WARN_US) gaps.push({ t0Us: prevT, t1Us: t });
        }
        prevT = t;
        const b = Math.min(n - 1, Math.floor((t - t0Us) / bucketUs));
        for (let c = 0; c < CHANNELS.length; c++) {
          const val = readChannel(v, o, CHANNELS[c]);
          if (val < min[c][b]) min[c][b] = val;
          if (val > max[c][b]) max[c][b] = val;
        }
      }
      bytes += count * RECORD_SIZE;
      onProgress?.(bytes, total);
    }
  }
  return { t0Us, t1Us, bucketUs, n, min, max, gaps, rtcEpoch, rtcTUs, files: index };
}

function finite(x: number): number {
  return Number.isFinite(x) ? x : NaN;
}

export async function queryRange(files: Blob[], ov: Overview, t0Us: number, t1Us: number, width: number, chans: number[]): Promise<RangeData> {
  t0Us = Math.max(ov.t0Us, t0Us);
  t1Us = Math.min(ov.t1Us, t1Us);
  width = Math.max(1, Math.floor(width));
  const resUs = (t1Us - t0Us) / width;
  const spanRecords = (t1Us - t0Us) / 1000;
  if (resUs >= ov.bucketUs || spanRecords > MAX_FILE_QUERY_RECORDS) {
    const b0 = Math.max(0, Math.floor((t0Us - ov.t0Us) / ov.bucketUs));
    const b1 = Math.min(ov.n - 1, Math.ceil((t1Us - ov.t0Us) / ov.bucketUs));
    const cnt = b1 - b0 + 1;
    const t = new Float64Array(cnt);
    for (let i = 0; i < cnt; i++) t[i] = ov.t0Us + (b0 + i + 0.5) * ov.bucketUs;
    const min = chans.map((c) => Float32Array.from(ov.min[c].subarray(b0, b1 + 1), finite));
    const max = chans.map((c) => Float32Array.from(ov.max[c].subarray(b0, b1 + 1), finite));
    return { t, min, max, raw: false };
  }

  // ファイルからその範囲だけ読む (両端に 1 画素分の余裕)
  const lo = t0Us - resUs;
  const hi = t1Us + resUs;
  const ts: number[] = [];
  const vals: number[][] = chans.map(() => []);
  for (const fi of ov.files) {
    if (fi.lastTUs < lo) continue;
    if (fi.firstTUs > hi) break;
    const file = files[fi.file];
    const first = await lowerBound(file, fi.records, lo);
    let end = await lowerBound(file, fi.records, hi);
    if (end < fi.records) end++; // hi を超える最初の 1 つも含める
    for (let start = first; start < end; start += CHUNK_RECORDS) {
      const count = Math.min(CHUNK_RECORDS, end - start);
      const off = HEADER_SIZE + start * RECORD_SIZE;
      const v = new DataView(await file.slice(off, off + count * RECORD_SIZE).arrayBuffer());
      for (let i = 0; i < count; i++) {
        const o = i * RECORD_SIZE;
        ts.push(recordTUs(v, o));
        for (let k = 0; k < chans.length; k++) vals[k].push(readChannel(v, o, CHANNELS[chans[k]]));
      }
    }
  }
  if (ts.length <= width * 2) {
    const t = Float64Array.from(ts);
    const min = vals.map((a) => Float32Array.from(a));
    return { t, min, max: min.map((a) => a.slice()), raw: true };
  }
  const nb = Math.ceil((hi - lo) / resUs);
  const t = new Float64Array(nb);
  for (let i = 0; i < nb; i++) t[i] = lo + (i + 0.5) * resUs;
  const min = chans.map(() => new Float32Array(nb).fill(Infinity));
  const max = chans.map(() => new Float32Array(nb).fill(-Infinity));
  for (let i = 0; i < ts.length; i++) {
    const b = Math.min(nb - 1, Math.floor((ts[i] - lo) / resUs));
    for (let k = 0; k < chans.length; k++) {
      const val = vals[k][i];
      if (val < min[k][b]) min[k][b] = val;
      if (val > max[k][b]) max[k][b] = val;
    }
  }
  return { t, min: min.map((a) => a.map(finite)), max: max.map((a) => a.map(finite)), raw: false };
}
