import { useRef, useEffect, useState } from "preact/hooks";
import uPlot from "uplot";
import "uplot/dist/uPlot.min.css";
import { drivetrainStore, DT_COL } from "../drivetrain-store";

const COLORS = ["#0ea5e9", "#38bdf8", "#22c55e", "#4ade80", "#fbbf24"];
const LABELS = ["Wheel FL", "Wheel FR", "Wheel RL", "Wheel RR", "RPM (Hz)"];
const COL_IDX = [DT_COL.fl, DT_COL.fr, DT_COL.rl, DT_COL.rr, DT_COL.rpm];

function memBuf(): uPlot.AlignedData {
  const m = drivetrainStore.mem;
  return [m[DT_COL.ts], ...COL_IDX.map((ci) => m[ci])] as uPlot.AlignedData;
}

const PRESETS: { label: string; value: number }[] = [
  { label: "30s", value: 30 },
  { label: "10s", value: 10 },
  { label: "5s", value: 5 },
];

export function DrivetrainChart() {
  const wrapRef = useRef<HTMLDivElement>(null);
  const plotRef = useRef<uPlot | null>(null);
  const [windowSec, setWindowSec] = useState(10);
  const windowRef = useRef(windowSec);
  windowRef.current = windowSec;

  useEffect(() => {
    const iv = setInterval(() => {
      const plot = plotRef.current;
      if (!plot || drivetrainStore.memRows === 0) return;
      const buf = memBuf();
      plot.setData(buf);
      const ts = buf[0] as number[];
      const ws = windowRef.current;
      if (ts.length > 0) {
        const latest = ts[ts.length - 1];
        plot.setScale("x", { min: latest - ws, max: latest });
      }
    }, 100);
    return () => clearInterval(iv);
  }, []);

  useEffect(() => {
    if (!wrapRef.current) return;
    const opts: uPlot.Options = {
      width: wrapRef.current.offsetWidth,
      height: 280,
      cursor: { drag: { x: false, y: false }, y: false },
      legend: { live: true },
      series: [
        {
          label: "Time",
          value: (_u, ts) => {
            if (ts == null) return "—";
            const d = new Date(ts * 1000);
            return `${String(d.getMinutes()).padStart(2, "0")}:${String(d.getSeconds()).padStart(2, "0")}.${String(d.getMilliseconds()).padStart(3, "0")}`;
          },
        },
        ...LABELS.map((label, i) => ({
          label,
          stroke: COLORS[i],
          width: 1.5,
          scale: i === 4 ? "rpm" : "hz",
        })),
      ],
      scales: {
        hz: { auto: true },
        rpm: { auto: true },
      },
      axes: [
        { stroke: "#8888aa", grid: { stroke: "#1e3a5f" } },
        { scale: "hz", stroke: "#8888aa", grid: { stroke: "#1e3a5f" } },
        { scale: "rpm", side: 1, stroke: "#fbbf24", grid: { show: false } },
      ],
    };
    plotRef.current = new uPlot(opts, memBuf(), wrapRef.current);
    const onResize = () => {
      if (wrapRef.current && plotRef.current) {
        plotRef.current.setSize({ width: wrapRef.current.offsetWidth, height: 280 });
      }
    };
    window.addEventListener("resize", onResize);
    return () => {
      window.removeEventListener("resize", onResize);
      plotRef.current?.destroy();
      plotRef.current = null;
    };
  }, []);

  return (
    <section>
      <div class="chart-header">
        <h2>Drivetrain Time Series</h2>
        <div class="chart-controls">
          {PRESETS.map((p) => (
            <button
              key={p.label}
              class={`chart-btn${windowSec === p.value ? " active" : ""}`}
              onClick={() => setWindowSec(p.value)}
            >
              {p.label}
            </button>
          ))}
        </div>
      </div>
      <div class="chart-wrap" ref={wrapRef} />
    </section>
  );
}
