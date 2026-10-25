import { useEffect, useRef } from "preact/hooks";
import uPlot from "uplot";
import "uplot/dist/uPlot.min.css";

// uPlot 1 枚。各チャネルは max (線) と min (線、凡例には出さない) の 2 系列で、間を薄く塗る (生の値なら min = max で線 1 本)。
// x の範囲は親が持ち (全パネル共通)、ここからは onRange で要求するだけ。

export interface PanelSeries {
  key: string;
  label: string;
  color: string;
}

export interface PanelData {
  t: number[]; // x の値 (秒、または時計モードなら Unix 秒)
  min: (number | null)[][];
  max: (number | null)[][];
}

interface Props {
  title: string;
  unit: string;
  series: PanelSeries[];
  data: PanelData | null;
  stepped?: boolean;
  range: [number, number];
  gaps: [number, number][];
  timeAxis: boolean; // x を時計 (Unix 秒) として描く
  width: number;
  onRange: (min: number, max: number) => void;
}

const HEIGHT = 170;

export function LogPanel({ title, unit, series, data, stepped, range, gaps, timeAxis, width, onRange }: Props) {
  const wrapRef = useRef<HTMLDivElement>(null);
  const plotRef = useRef<uPlot | null>(null);
  const rangeRef = useRef(range);
  rangeRef.current = range;
  const gapsRef = useRef(gaps);
  gapsRef.current = gaps;
  const onRangeRef = useRef(onRange);
  onRangeRef.current = onRange;

  const seriesKey = series.map((s) => s.key).join(",");
  useEffect(() => {
    if (!wrapRef.current) return;
    const paths = stepped ? uPlot.paths.stepped!({ align: 1 }) : undefined;
    const opts: uPlot.Options = {
      width,
      height: HEIGHT,
      title,
      scales: { x: { time: timeAxis } },
      axes: [
        { stroke: "#9a9abe", grid: { stroke: "#1f1f36" }, ticks: { stroke: "#2a2a44" } },
        { stroke: "#9a9abe", grid: { stroke: "#1f1f36" }, ticks: { stroke: "#2a2a44" }, size: 76, label: unit || undefined, labelSize: unit ? 14 : 0 },
      ],
      cursor: {
        sync: { key: "logchart", setSeries: false },
        drag: { x: true, y: false, setScale: false },
        bind: {
          dblclick: () => null, // uPlot 既定の「ダブルクリックで全体」は、このパネルだけ親の範囲とずれるので使わない (全体ボタンで)
          // Shift + ドラッグで移動。それ以外は uPlot の範囲選択
          mousedown: (u, _targ, handler) => (e: MouseEvent) => {
            if (!e.shiftKey) return handler(e);
            e.preventDefault();
            const startX = e.clientX;
            const [min0, max0] = rangeRef.current;
            const pxPerVal = u.bbox.width / devicePixelRatio / (max0 - min0);
            const move = (ev: MouseEvent) => {
              const dv = -(ev.clientX - startX) / pxPerVal;
              onRangeRef.current(min0 + dv, max0 + dv);
            };
            const up = () => {
              window.removeEventListener("mousemove", move);
              window.removeEventListener("mouseup", up);
            };
            window.addEventListener("mousemove", move);
            window.addEventListener("mouseup", up);
            return null;
          },
        },
      },
      legend: { live: true },
      series: [
        {},
        ...series.flatMap((s): uPlot.Series[] => [
          { label: s.label, stroke: s.color, width: 1, paths, value: (_u, v) => (v == null ? "" : fmt(v)) },
          { label: `${s.label} min`, stroke: s.color, width: 1, paths, class: "u-minseries" },
        ]),
      ],
      bands: series.map((s, i) => ({ series: [1 + 2 * i, 2 + 2 * i], fill: s.color + "30" })),
      hooks: {
        setSelect: [
          (u) => {
            if (u.select.width <= 0) return;
            const min = u.posToVal(u.select.left, "x");
            const max = u.posToVal(u.select.left + u.select.width, "x");
            u.setSelect({ left: 0, top: 0, width: 0, height: 0 }, false);
            onRangeRef.current(min, max);
          },
        ],
        draw: [
          (u) => {
            const [xmin, xmax] = [u.scales.x.min!, u.scales.x.max!];
            const ctx = u.ctx;
            ctx.save();
            ctx.fillStyle = "rgba(248, 113, 113, 0.18)";
            for (const [g0, g1] of gapsRef.current) {
              if (g1 < xmin || g0 > xmax) continue;
              const x0 = u.valToPos(Math.max(g0, xmin), "x", true);
              const x1 = u.valToPos(Math.min(g1, xmax), "x", true);
              ctx.fillRect(x0, u.bbox.top, Math.max(1, x1 - x0), u.bbox.height);
            }
            ctx.restore();
          },
        ],
      },
    };
    const u = new uPlot(opts, [[], ...series.flatMap(() => [[], []])] as uPlot.AlignedData, wrapRef.current);
    const wheel = (e: WheelEvent) => {
      e.preventDefault();
      const rect = u.over.getBoundingClientRect();
      const xVal = u.posToVal(e.clientX - rect.left, "x");
      const [min, max] = rangeRef.current;
      const f = e.deltaY < 0 ? 0.8 : 1.25;
      onRangeRef.current(xVal - (xVal - min) * f, xVal + (max - xVal) * f);
    };
    u.over.addEventListener("wheel", wheel, { passive: false });
    plotRef.current = u;
    return () => {
      u.over.removeEventListener("wheel", wheel);
      u.destroy();
      plotRef.current = null;
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [seriesKey, stepped, timeAxis, title, unit]);

  useEffect(() => {
    const u = plotRef.current;
    if (!u || !data) return;
    u.setData([data.t, ...series.flatMap((_, i) => [data.max[i], data.min[i]])] as uPlot.AlignedData, false);
    u.setScale("x", { min: rangeRef.current[0], max: rangeRef.current[1] }); // setData は範囲を変えないので、改めて
  }, [data, seriesKey]);

  useEffect(() => {
    plotRef.current?.setScale("x", { min: range[0], max: range[1] });
  }, [range]);

  useEffect(() => {
    plotRef.current?.setSize({ width, height: HEIGHT });
  }, [width]);

  return <div class="log-chart-panel" ref={wrapRef} />;
}

function fmt(v: number): string {
  return Math.abs(v) >= 1000 ? v.toFixed(0) : Number.isInteger(v) ? String(v) : v.toFixed(2);
}
