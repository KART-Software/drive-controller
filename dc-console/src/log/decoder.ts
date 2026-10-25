// ログの run (dc_NNNN ディレクトリ) の一覧用。ファイルの先頭と末尾しか読まない (全走査は csv.worker)
import { HEADER_SIZE, RECORD_SIZE, LogFormatError, parseHeader, recordDrops, recordTUs, type LogHeader } from "./log_record";

export const RUN_DIR_RE = /^dc_(\d{4})(?:_(\d{4})(\d{2})(\d{2})_(\d{2})(\d{2})(\d{2}))?/;
const FILE_RE = /^\d{4}\.bin$/;

export interface RunSummary {
  name: string;
  dir: FileSystemDirectoryHandle;
  files: File[]; // file_index 順
  date: Date | null; // rtc_epoch、無ければディレクトリ名の日時
  durationS: number | null;
  validBytes: number; // 有効なレコードまでの長さ (事前確保分は含まない)
  drops: number | null; // 最後のレコードの log_drops
  error: string | null; // header の検査に落ちた理由 (書き出し不可)
}

export async function readHeader(file: File): Promise<LogHeader | null> {
  return parseHeader(await file.slice(0, HEADER_SIZE).arrayBuffer(), file.name);
}

export function recordCount(file: File): number {
  return Math.max(0, Math.floor((file.size - HEADER_SIZE) / RECORD_SIZE));
}

export async function tUsAt(file: File, i: number): Promise<number> {
  const off = HEADER_SIZE + i * RECORD_SIZE;
  return recordTUs(new DataView(await file.slice(off, off + 8).arrayBuffer()), 0);
}

/** 最後の有効レコード (t_us ≠ 0) の番号、無ければ -1。有効な部分は先頭から連続し、後ろはゼロ埋めなので二分探索 */
export async function findLastRecord(file: File): Promise<number> {
  const n = recordCount(file);
  if (n === 0 || (await tUsAt(file, 0)) === 0) return -1;
  let lo = 0;
  let hi = n - 1;
  if ((await tUsAt(file, hi)) !== 0) return hi;
  while (hi - lo > 1) {
    const mid = (lo + hi) >> 1;
    if ((await tUsAt(file, mid)) !== 0) lo = mid;
    else hi = mid;
  }
  return lo;
}

async function readRecord(file: File, i: number): Promise<DataView> {
  const off = HEADER_SIZE + i * RECORD_SIZE;
  return new DataView(await file.slice(off, off + RECORD_SIZE).arrayBuffer());
}

export async function listRunFiles(dir: FileSystemDirectoryHandle): Promise<File[]> {
  const files: File[] = [];
  for await (const entry of dir.values()) {
    if (entry.kind === "file" && FILE_RE.test(entry.name)) files.push(await entry.getFile());
  }
  files.sort((a, b) => (a.name < b.name ? -1 : a.name > b.name ? 1 : 0));
  return files;
}

function dateFromDirName(name: string): Date | null {
  const m = RUN_DIR_RE.exec(name);
  if (!m || !m[2]) return null;
  return new Date(+m[2], +m[3] - 1, +m[4], +m[5], +m[6], +m[7]);
}

export async function scanRun(dir: FileSystemDirectoryHandle): Promise<RunSummary> {
  const run: RunSummary = { name: dir.name, dir, files: [], date: dateFromDirName(dir.name), durationS: null, validBytes: 0, drops: null, error: null };
  try {
    run.files = await listRunFiles(dir);
    let firstT: number | null = null;
    let lastT: number | null = null;
    for (const file of run.files) {
      const h = await readHeader(file);
      if (!h) continue; // 空ファイル
      if (h.rtcEpoch && !run.date) run.date = new Date(h.rtcEpoch * 1000);
      const last = await findLastRecord(file);
      if (last < 0) continue;
      run.validBytes += HEADER_SIZE + (last + 1) * RECORD_SIZE;
      if (firstT === null) firstT = await tUsAt(file, 0);
      const rec = await readRecord(file, last);
      lastT = recordTUs(rec, 0);
      run.drops = recordDrops(rec, 0);
    }
    if (firstT !== null && lastT !== null) run.durationS = (lastT - firstT) / 1e6;
  } catch (err) {
    run.error = err instanceof LogFormatError ? err.message : `読めない: ${(err as Error).message}`;
  }
  return run;
}

/** ルート (dc_NNNN… が並ぶ) か run ディレクトリそのものを受けて run の一覧 (新しい順) */
export async function scanRuns(root: FileSystemDirectoryHandle, onProgress?: (done: number, total: number) => void): Promise<RunSummary[]> {
  if (RUN_DIR_RE.test(root.name)) return [await scanRun(root)];
  const dirs: FileSystemDirectoryHandle[] = [];
  for await (const entry of root.values()) {
    if (entry.kind === "directory" && RUN_DIR_RE.test(entry.name)) dirs.push(entry);
  }
  dirs.sort((a, b) => (a.name < b.name ? 1 : a.name > b.name ? -1 : 0));
  const runs: RunSummary[] = [];
  for (const d of dirs) {
    runs.push(await scanRun(d));
    onProgress?.(runs.length, dirs.length);
  }
  return runs;
}
