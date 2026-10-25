import { useEffect, useRef, useState } from "preact/hooks";
import { scanRuns, type RunSummary } from "../log/decoder";
import { ensureReadPermission, loadDirHandle, saveDirHandle } from "../log/dir-handle";
import type { CsvWorkerIn, CsvWorkerOut } from "../log/csv.worker";
import type { TimeRange } from "../log/csv-encode";
import { RECORD_SIZE } from "../log/log_record";
import { LogChart } from "./LogChart";

// SD ログ (USB ドライブ "KART SD Reader" か、コピーしたフォルダ) の run 一覧と CSV 書き出し (docs/log_viewer_spec.md)。
// シリアル接続は要らない。Chrome / Edge の File System Access API を使う。

interface Props {
  addLog: (msg: string) => void;
  openRequest: number; // 増えたらフォルダ選択を開く (SD リーダー画面の「ログを開く」から。クリックの中で呼ぶ必要がある)
}

interface Job {
  run: string;
  bytes: number;
  total: number;
  records: number;
  state: "running" | "done" | "cancelled" | "error";
  gaps?: number;
  message?: string;
}

const supported = typeof window.showDirectoryPicker === "function";

function fmtDuration(s: number | null): string {
  if (s === null) return "—";
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  const sec = Math.floor(s % 60);
  return h ? `${h}:${String(m).padStart(2, "0")}:${String(sec).padStart(2, "0")}` : `${m}:${String(sec).padStart(2, "0")}`;
}

function fmtBytes(b: number): string {
  return b >= 1 << 30 ? `${(b / (1 << 30)).toFixed(2)} GB` : `${(b / (1 << 20)).toFixed(1)} MB`;
}

export function LogViewer({ addLog, openRequest }: Props) {
  const [dir, setDir] = useState<FileSystemDirectoryHandle | null>(null);
  const [saved, setSaved] = useState<FileSystemDirectoryHandle | null>(null);
  const [runs, setRuns] = useState<RunSummary[]>([]);
  const [scan, setScan] = useState<string | null>(null); // 走査中の表示
  const [error, setError] = useState<string | null>(null);
  const [decimate, setDecimate] = useState(1);
  const [job, setJob] = useState<Job | null>(null);
  const [openRun, setOpenRun] = useState<RunSummary | null>(null); // チャートで開いている run
  const workerRef = useRef<Worker | null>(null);
  const lastOpenRequest = useRef(openRequest);

  useEffect(() => { loadDirHandle().then(setSaved); }, []);

  useEffect(() => {
    if (openRequest !== lastOpenRequest.current) {
      lastOpenRequest.current = openRequest;
      openPicker();
    }
  }, [openRequest]);

  async function open(h: FileSystemDirectoryHandle) {
    setDir(h);
    setError(null);
    setRuns([]);
    setOpenRun(null);
    setScan("読み込み中…");
    try {
      const list = await scanRuns(h, (done, total) => setScan(`読み込み中… ${done} / ${total}`));
      setRuns(list);
      if (list.length === 0) setError(`${h.name} に dc_NNNN のフォルダがありません`);
    } catch (err) {
      setError(`読めない: ${(err as Error).message}`);
    } finally {
      setScan(null);
    }
  }

  async function openPicker() {
    if (!supported) return;
    try {
      const h = await window.showDirectoryPicker({ id: "kart-logs", mode: "read" });
      await saveDirHandle(h);
      setSaved(h);
      await open(h);
    } catch (err) {
      if ((err as Error).name !== "AbortError") setError((err as Error).message);
    }
  }

  async function openSaved() {
    if (!saved) return;
    try {
      if (await ensureReadPermission(saved)) await open(saved);
      else setError("フォルダへのアクセスが許可されませんでした");
    } catch (err) {
      setError(`前回のフォルダを開けない (${(err as Error).message})。もう一度選んでください`);
    }
  }

  async function exportCsv(run: RunSummary, range?: TimeRange, rangeLabel?: string) {
    let handle: FileSystemFileHandle;
    const rangeName = range ? `_${rangeLabel}` : "";
    try {
      handle = await window.showSaveFilePicker({
        suggestedName: `${run.name}${rangeName}.csv`,
        types: [{ description: "CSV", accept: { "text/csv": [".csv"] } }],
      });
    } catch (err) {
      if ((err as Error).name !== "AbortError") setError((err as Error).message);
      return;
    }
    const writable = await handle.createWritable();
    const worker = new Worker(new URL("../log/csv.worker.ts", import.meta.url), { type: "module" });
    workerRef.current = worker;
    const total = range ? Math.min(run.validBytes, Math.ceil((range.t1Us - range.t0Us) / 1000) * RECORD_SIZE) : run.validBytes;
    setJob({ run: run.name + rangeName, bytes: 0, total, records: 0, state: "running" });
    const send = (m: CsvWorkerIn) => worker.postMessage(m);
    const finish = async (state: Job["state"], extra: Partial<Job>) => {
      await writable.close(); // 途中で止めても書けた分は残す (書き直せる)
      worker.terminate();
      workerRef.current = null;
      setJob((j) => (j ? { ...j, state, ...extra } : j));
    };
    worker.onmessage = async (e: MessageEvent<CsvWorkerOut>) => {
      const m = e.data;
      if (m.type === "rows") {
        await writable.write(m.text);
        send({ type: "ack" });
      } else if (m.type === "progress") {
        setJob((j) => (j ? { ...j, bytes: m.bytes, records: m.records } : j));
      } else if (m.type === "done") {
        await finish("done", { gaps: m.gaps, records: m.records });
        addLog(`CSV: ${run.name} → ${handle.name} (${m.records} records, ${m.gaps} gaps${m.skipped.length ? `, skipped ${m.skipped.join(" ")}` : ""})`);
      } else if (m.type === "error") {
        await finish(m.message === "cancelled" ? "cancelled" : "error", { message: m.message });
        if (m.message !== "cancelled") addLog(`CSV error: ${m.message}`);
      }
    };
    worker.onerror = (e) => { finish("error", { message: e.message }); };
    send({ type: "start", files: run.files, decimate, range });
  }

  function cancel() {
    workerRef.current?.postMessage({ type: "cancel" } satisfies CsvWorkerIn);
  }

  if (!supported) {
    return (
      <section style={{ margin: "24px", maxWidth: "640px" }}>
        <h2>Logs</h2>
        <p style={{ color: "var(--err)" }}>このブラウザではフォルダを開けません (File System Access API)。Chrome か Edge を使ってください。</p>
      </section>
    );
  }

  const busy = job?.state === "running";
  return (
    <section style={{ margin: "24px" }}>
      <h2>Logs</h2>
      <p style={{ color: "var(--text-dim)" }}>
        SD カード (USB ドライブ「KART SD Reader」) か、ログをコピーしたフォルダを開いて、run ごとに CSV に書き出します。列は tools/decode_log.py と同じです。
      </p>
      <div class="log-toolbar">
        <button onClick={openPicker} disabled={busy}>フォルダを開く</button>
        {saved && !dir && <button onClick={openSaved} disabled={busy}>前回のフォルダを開く ({saved.name})</button>}
        {dir && <button onClick={() => open(dir)} disabled={busy || !!scan}>再読み込み</button>}
        {dir && <span style={{ color: "var(--text-dim)" }}>{dir.name}</span>}
        <span style={{ flex: 1 }} />
        <label>間引き
          <select value={decimate} disabled={busy} onChange={(e) => setDecimate(+(e.target as HTMLSelectElement).value)} style={{ marginLeft: "6px" }}>
            <option value={1}>なし (1 kHz)</option>
            <option value={10}>1/10 (100 Hz)</option>
            <option value={100}>1/100 (10 Hz)</option>
          </select>
        </label>
      </div>
      {scan && <p>{scan}</p>}
      {error && <p style={{ color: "var(--err)" }}>{error}</p>}
      {openRun && (
        <div class="log-toolbar log-open-header">
          <button onClick={() => setOpenRun(null)} disabled={busy}>← 一覧に戻る</button>
          <strong>{openRun.name}</strong>
          <span style={{ color: "var(--text-dim)" }}>{openRun.date ? openRun.date.toLocaleString() : "—"} · {fmtDuration(openRun.durationS)} · {fmtBytes(openRun.validBytes)}</span>
        </div>
      )}
      {!openRun && runs.length > 0 && (
        <table class="log-runs">
          <thead>
            <tr><th>run</th><th>日時</th><th class="num">長さ</th><th class="num">ファイル</th><th class="num">サイズ</th><th class="num">drops</th><th>状態</th><th /></tr>
          </thead>
          <tbody>
            {runs.map((r) => (
              <tr key={r.name} class={r.error ? "error" : ""}>
                <td>{r.name}</td>
                <td>{r.date ? r.date.toLocaleString() : "—"}</td>
                <td class="num">{fmtDuration(r.durationS)}</td>
                <td class="num">{r.files.length}</td>
                <td class="num">{fmtBytes(r.validBytes)}</td>
                <td class="num">{r.drops ?? "—"}</td>
                <td>{r.error ?? (r.durationS === null ? "空" : "")}</td>
                <td>
                  <button disabled={busy || !!r.error || r.durationS === null} onClick={() => setOpenRun(r)}>開く</button>{" "}
                  <button disabled={busy || !!r.error || r.durationS === null} onClick={() => exportCsv(r)}>CSV</button>
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      )}
      {openRun && <LogChart run={openRun} busy={busy} onExportRange={(t0Us, t1Us, label) => exportCsv(openRun, { t0Us, t1Us }, label)} />}
      {job && (
        <div class="log-progress">
          <span>{job.run}</span>
          <progress value={job.bytes} max={Math.max(job.total, 1)} />
          <span style={{ fontFamily: "monospace" }}>{fmtBytes(job.bytes)} / {fmtBytes(job.total)} · {job.records.toLocaleString()} records</span>
          {job.state === "running" && <button onClick={cancel}>キャンセル</button>}
          {job.state === "done" && <span style={{ color: "var(--ok, #4ade80)" }}>完了{job.gaps ? `、欠け ${job.gaps} 回` : ""}</span>}
          {job.state === "cancelled" && <span style={{ color: "var(--text-dim)" }}>キャンセル (書けた分は残っています)</span>}
          {job.state === "error" && <span style={{ color: "var(--err)" }}>失敗: {job.message}</span>}
        </div>
      )}
    </section>
  );
}
