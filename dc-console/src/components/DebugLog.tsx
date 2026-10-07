import { useEffect, useRef, useState } from "preact/hooks";
import { sensorStore } from "../sensor-store";
import type { SysStatsT } from "../types";

interface LogEntry {
  ts: string;
  msg: string;
}

interface Props {
  entries: LogEntry[];
}

// loop 停止の計測 (SysStats, docs/loop_nonblocking_spec.md §5) を 1 行で表示。
// 毎秒 1 回の SD flush で loop max が 3〜9 ms になるのは正常。数十 ms 以上は USB/SD のストール。
// - 2 s 新しいフレームが来なければ隠す (切断後に古い値を表示し続けない)
// - logDrops は起動以来の累積なので、増えた時から 10 s だけ警告色にする
// - 値が変わった時だけ再描画する
function SysStatsLine() {
  const [view, setView] = useState<{ sys: SysStatsT; warn: boolean } | null>(null);
  const prev = useRef({ ts: -1, stale: 0, drops: -1, dropAt: 0, key: "" });
  useEffect(() => {
    const iv = setInterval(() => {
      const p = prev.current;
      const d = sensorStore.latest;
      const hide = () => {
        if (p.key !== "") {
          p.key = "";
          setView(null);
        }
      };
      if (!d?.sys) return hide();
      if (d.ts === p.ts) {
        if (++p.stale >= 4) hide();
        return;
      }
      p.ts = d.ts;
      p.stale = 0;
      const s = d.sys;
      if (p.drops >= 0 && s.logDrops > p.drops) p.dropAt = Date.now();
      p.drops = s.logDrops;
      const warn = s.loopMaxUs >= 20000 || Date.now() - p.dropAt < 10000;
      const key = `${s.loopMaxUs},${s.loopMeanUs},${s.loopMaxUsBoot},${s.sdMaxUs},${s.safetyMaxUs},${s.logDrops},${warn}`;
      if (key !== p.key) {
        p.key = key;
        setView({ sys: s, warn });
      }
    }, 500);
    return () => clearInterval(iv);
  }, []);
  if (!view) return null;
  const { sys, warn } = view;
  const ms = (us: number) => (us / 1000).toFixed(1);
  return (
    <div class="sys-stats" style={{ color: warn ? "var(--warn)" : "var(--text-dim)", fontSize: "0.8em" }}>
      loop max {ms(sys.loopMaxUs)} ms (mean {ms(sys.loopMeanUs)}, boot max {ms(sys.loopMaxUsBoot)}) | SD max{" "}
      {ms(sys.sdMaxUs)} ms | safety ISR max {ms(sys.safetyMaxUs)} ms | log drops {sys.logDrops}
    </div>
  );
}

export function DebugLog({ entries }: Props) {
  return (
    <section>
      <h2>Debug Log</h2>
      <SysStatsLine />
      <div class="log-output">
        {entries.map((e, i) => (
          <div class="log-entry" key={i}>
            <span class="log-ts">{e.ts}</span>
            {e.msg}
          </div>
        ))}
      </div>
    </section>
  );
}

export type { LogEntry };
