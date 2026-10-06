import { useEffect, useState } from "preact/hooks";
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
// 毎秒 1 回の SD flush で loop max が 3〜9 ms になるのは正常。120 ms 級は USB/SD のストール。
function SysStatsLine() {
  const [sys, setSys] = useState<SysStatsT | undefined>(undefined);
  useEffect(() => {
    const iv = setInterval(() => setSys(sensorStore.latest?.sys), 500);
    return () => clearInterval(iv);
  }, []);
  if (!sys) return null;
  const ms = (us: number) => (us / 1000).toFixed(1);
  const warn = sys.loopMaxUs >= 20000 || sys.logDrops > 0;
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
