import { useEffect, useRef, useState } from "preact/hooks";
import { sensorStore } from "../sensor-store";
import type { CanStatusT, SysStatsT } from "../types";

interface LogEntry {
  ts: string;
  msg: string;
  deviceUs?: number;  // デバイスの debug メッセージの時刻 (並べ替え用)。console 自身のメッセージは undefined
}

interface Props {
  entries: LogEntry[];
}

// loop 停止の計測 (SysStats, docs/loop_nonblocking_spec.md §5) を 1 行で表示。
// 毎秒 1 回の SD flush で loop max が 3〜9 ms になるのは正常。数十 ms 以上は USB/SD のストール。
// - 2 s 新しいフレームが来なければ隠す (切断後に古い値を表示し続けない)
// - logDrops は起動以来の累積なので、増えた時から 10 s だけ警告色にする
// - CAN (CanStatus) も同じ行に出す。overrun / txdrop が増えた時から 10 s、TEC ≥ 96 (警告レベル)、fault ≠ active は警告色
// - 値が変わった時だけ再描画する
function SysStatsLine() {
  const [view, setView] = useState<{ sys: SysStatsT; can?: CanStatusT; warn: boolean } | null>(null);
  const prev = useRef({ ts: -1, stale: 0, drops: -1, dropAt: 0, canDrops: -1, canDropAt: 0, key: "" });
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
      const c = d.can;
      const canDrops = c ? c.rxOverrun + c.txDropped : 0;
      if (p.canDrops >= 0 && canDrops > p.canDrops) p.canDropAt = Date.now();
      p.canDrops = canDrops;
      const canWarn = !!c && (c.fault !== "active" || c.txErrors >= 96 || Date.now() - p.canDropAt < 10000);
      const warn = s.loopMaxUs >= 20000 || Date.now() - p.dropAt < 10000 || canWarn;
      const key = `${s.loopMaxUs},${s.loopMeanUs},${s.loopMaxUsBoot},${s.sdMaxUs},${s.tickMaxUs},${s.logDrops},${warn},` +
        (c ? `${c.fault},${c.rxRate},${c.rxOverrun},${c.txDropped},${c.txErrors},${c.rxErrors}` : "");
      if (key !== p.key) {
        p.key = key;
        setView({ sys: s, can: c, warn });
      }
    }, 500);
    return () => clearInterval(iv);
  }, []);
  if (!view) return null;
  const { sys, can, warn } = view;
  const ms = (us: number) => (us / 1000).toFixed(1);
  return (
    <div class="sys-stats" style={{ color: warn ? "var(--warn)" : "var(--text-dim)", fontSize: "0.8em" }}>
      loop max {ms(sys.loopMaxUs)} ms (mean {ms(sys.loopMeanUs)}, boot max {ms(sys.loopMaxUsBoot)}) | SD max{" "}
      {ms(sys.sdMaxUs)} ms | tick max {ms(sys.tickMaxUs)} ms | log drops {sys.logDrops}
      {can && ` | CAN ${can.fault} rx ${can.rxRate}/s overrun ${can.rxOverrun} txdrop ${can.txDropped} TEC ${can.txErrors} REC ${can.rxErrors}`}
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
