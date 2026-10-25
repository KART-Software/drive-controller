import { useEffect, useRef, useState } from "preact/hooks";
import { CHANNELS, CHANNEL_INDEX, PANELS } from "../log/channels";
import type { ChartWorkerIn, ChartWorkerOut, OverviewMeta } from "../log/chart.worker";
import type { RunSummary } from "../log/decoder";
import { LogPanel, type PanelData } from "./LogPanel";

// 1 run のチャート (docs/log_viewer_spec.md §6)。Worker が概観を持ち、表示範囲が変わるたびに query する。
// x の範囲 (t_us) はここが持ち、全パネルに配る。

interface Props {
  run: RunSummary;
  busy: boolean; // CSV 書き出し中
  onExportRange: (t0Us: number, t1Us: number, label: string) => void; // label は画面と同じ run 開始からの秒 (ファイル名用)
}

const COLORS = ["#0ea5e9", "#f59e0b", "#22c55e", "#a78bfa", "#f472b6", "#38bdf8", "#4ade80", "#fb923c", "#e879f9", "#facc15", "#2dd4bf"];
const PANELS_KEY = "logChart.panels";
const MIN_SPAN_US = 10_000;

function loadPanels(): Set<string> {
  try {
    const v = localStorage.getItem(PANELS_KEY);
    if (v) return new Set(JSON.parse(v) as string[]);
  } catch { /* 既定へ */ }
  return new Set(["throttle", "speed", "rpm"]);
}

function fmtSec(s: number): string {
  const m = Math.floor(s / 60);
  return `${m}:${(s - m * 60).toFixed(1).padStart(4, "0")}`;
}

export function LogChart({ run, busy, onExportRange }: Props) {
  const [phase, setPhase] = useState<"loading" | "ready" | "error">("loading");
  const [progress, setProgress] = useState<[number, number]>([0, 1]);
  const [error, setError] = useState<string | null>(null);
  const [meta, setMeta] = useState<OverviewMeta | null>(null);
  const [range, setRange] = useState<[number, number]>([0, 1]); // t_us
  const [panelsOn, setPanelsOn] = useState<Set<string>>(loadPanels);
  const [clock, setClock] = useState(false);
  const [width, setWidth] = useState(800);
  const [data, setData] = useState<Record<string, PanelData>>({});
  const workerRef = useRef<Worker | null>(null);
  const genRef = useRef(0);
  const wrapRef = useRef<HTMLDivElement>(null);

  const toX = (tUs: number) => (clock && meta?.rtcEpoch ? meta.rtcEpoch + (tUs - meta.rtcTUs) / 1e6 : (tUs - (meta?.t0Us ?? 0)) / 1e6);
  const fromX = (x: number) => (clock && meta?.rtcEpoch ? meta.rtcTUs + (x - meta.rtcEpoch) * 1e6 : x * 1e6 + (meta?.t0Us ?? 0));
  const toXRef = useRef(toX);
  toXRef.current = toX;
  const panelsOnRef = useRef(panelsOn);
  panelsOnRef.current = panelsOn;

  // 幅はコンテナに追従
  useEffect(() => {
    if (!wrapRef.current) return;
    const ro = new ResizeObserver((entries) => setWidth(Math.max(300, Math.floor(entries[0].contentRect.width))));
    ro.observe(wrapRef.current);
    return () => ro.disconnect();
  }, [phase]);

  // run ごとに Worker を作り、概観を作らせる
  useEffect(() => {
    setPhase("loading");
    setError(null);
    setMeta(null);
    setData({});
    const worker = new Worker(new URL("../log/chart.worker.ts", import.meta.url), { type: "module" });
    workerRef.current = worker;
    worker.onmessage = (e: MessageEvent<ChartWorkerOut>) => {
      const m = e.data;
      if (m.type === "progress") setProgress([m.bytes, m.total]);
      else if (m.type === "ready") {
        setMeta(m.meta);
        setRange([m.meta.t0Us, m.meta.t1Us]);
        setPhase("ready");
      } else if (m.type === "data") {
        if (m.gen !== genRef.current) return; // 古いクエリ
        setData(splitByPanel(m, panelsOnRef.current, toXRef.current));
      } else if (m.type === "error") {
        setError(m.message);
        setPhase("error");
      }
    };
    worker.postMessage({ type: "open", files: run.files } satisfies ChartWorkerIn);
    return () => { worker.terminate(); workerRef.current = null; };
  }, [run]);

  // 範囲 / パネル / 幅 / 軸が変わったらクエリ (少し待ってまとめる)
  useEffect(() => {
    if (phase !== "ready" || !meta) return;
    const timer = setTimeout(() => {
      const channels = [...new Set(PANELS.filter((p) => panelsOn.has(p.id)).flatMap((p) => p.channels))].map((k) => CHANNEL_INDEX[k]);
      if (channels.length === 0) { setData({}); return; }
      const gen = ++genRef.current;
      workerRef.current?.postMessage({ type: "query", gen, t0Us: range[0], t1Us: range[1], width: Math.max(100, width - 70), channels } satisfies ChartWorkerIn);
    }, 30);
    return () => clearTimeout(timer);
  }, [phase, meta, range, panelsOn, width, clock]);

  function onRange(minX: number, maxX: number) {
    if (!meta) return;
    let a = fromX(minX);
    let b = fromX(maxX);
    if (b - a < MIN_SPAN_US) { const c = (a + b) / 2; a = c - MIN_SPAN_US / 2; b = c + MIN_SPAN_US / 2; }
    const span = b - a;
    if (a < meta.t0Us) { a = meta.t0Us; b = Math.min(meta.t1Us, a + span); }
    if (b > meta.t1Us) { b = meta.t1Us; a = Math.max(meta.t0Us, b - span); }
    setRange([a, b]);
  }

  function togglePanel(id: string) {
    const next = new Set(panelsOn);
    if (next.has(id)) next.delete(id);
    else next.add(id);
    setPanelsOn(next);
    try { localStorage.setItem(PANELS_KEY, JSON.stringify([...next])); } catch { /* 覚えないだけ */ }
  }

  if (phase === "loading") {
    return (
      <div ref={wrapRef} class="log-progress">
        <span>{run.name} を読み込み中</span>
        <progress value={progress[0]} max={Math.max(progress[1], 1)} />
        <span style={{ fontFamily: "monospace" }}>{(progress[0] / 1048576).toFixed(0)} / {(progress[1] / 1048576).toFixed(0)} MB</span>
      </div>
    );
  }
  if (phase === "error" || !meta) return <p ref={wrapRef} style={{ color: "var(--err)" }}>読めない: {error}</p>;

  const xRange: [number, number] = [toX(range[0]), toX(range[1])];
  const gaps = meta.gaps.map((g): [number, number] => [toX(g.t0Us), toX(g.t1Us)]);
  const spanS = (range[1] - range[0]) / 1e6;
  return (
    <div ref={wrapRef} class="log-chart">
      <div class="log-toolbar">
        <button onClick={() => setRange([meta.t0Us, meta.t1Us])}>全体</button>
        <span style={{ fontFamily: "monospace", color: "var(--text-dim)" }}>
          {fmtSec((range[0] - meta.t0Us) / 1e6)} – {fmtSec((range[1] - meta.t0Us) / 1e6)} ({spanS >= 1 ? `${spanS.toFixed(1)} s` : `${(spanS * 1000).toFixed(0)} ms`})
        </span>
        <button disabled={!meta.rtcEpoch} title={meta.rtcEpoch ? "" : "この run には RTC の時刻がありません"} onClick={() => setClock(!clock)}>
          {clock ? "時計 → 秒" : "秒 → 時計"}
        </button>
        <button disabled={busy} onClick={() => onExportRange(range[0], range[1], `${((range[0] - meta.t0Us) / 1e6).toFixed(1)}s-${((range[1] - meta.t0Us) / 1e6).toFixed(1)}s`)}>この範囲を CSV</button>
        <span style={{ flex: 1 }} />
        {meta.gaps.length > 0 && <span style={{ color: "var(--err)" }}>欠け {meta.gaps.length} 回</span>}
        <span style={{ color: "var(--text-dim)", fontSize: "0.8rem" }}>ドラッグで拡大 · ホイールで拡大縮小 · Shift+ドラッグで移動</span>
      </div>
      <div class="log-toolbar">
        {PANELS.map((p) => (
          <label key={p.id} class="log-panel-toggle">
            <input type="checkbox" checked={panelsOn.has(p.id)} onChange={() => togglePanel(p.id)} /> {p.title}
          </label>
        ))}
      </div>
      {PANELS.filter((p) => panelsOn.has(p.id)).map((p) => (
        <LogPanel
          key={p.id}
          title={p.title}
          unit={p.unit}
          stepped={p.stepped}
          series={p.channels.map((k, i) => ({ key: k, label: k, color: COLORS[i % COLORS.length] }))}
          data={data[p.id] ?? null}
          range={xRange}
          gaps={gaps}
          timeAxis={clock && !!meta.rtcEpoch}
          width={width}
          onRange={onRange}
        />
      ))}
    </div>
  );
}

function splitByPanel(m: Extract<ChartWorkerOut, { type: "data" }>, panelsOn: Set<string>, toX: (tUs: number) => number): Record<string, PanelData> {
  // Worker が返したチャネル順 (m.channels) で引く。パネルの表示が変わった直後に古い順序の結果が来ても取り違えない
  const pos = new Map(m.channels.map((c, i) => [CHANNELS[c].key, i]));
  const t = Array.from(m.t, toX);
  const toArr = (a: Float32Array) => Array.from(a, (v) => (Number.isNaN(v) ? null : v));
  const out: Record<string, PanelData> = {};
  for (const p of PANELS) {
    if (!panelsOn.has(p.id) || !p.channels.every((k) => pos.has(k))) continue;
    out[p.id] = {
      t,
      min: p.channels.map((k) => toArr(m.min[pos.get(k)!])),
      max: p.channels.map((k) => toArr(m.max[pos.get(k)!])),
    };
  }
  return out;
}
