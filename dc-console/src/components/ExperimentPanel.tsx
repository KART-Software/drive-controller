import { useMemo, useState } from "preact/hooks";
import { protocol } from "../protocol";
import type { LogEntry } from "./DebugLog";

// ETC 同定実験モード (docs/etc_experiment_mode_spec.md §10)。
// 開始 3 ボタン + 停止。確認ダイアログは console 側のみ (Q15)。
// 進捗はファームの debug ("EXP start/progress/done/stop/abort ...") が
// DebugLog に流れるので専用表示は持たない (Q9)。実行中判定も同じ debug から導出。
const EXPERIMENTS = [
  {
    type: 1,
    label: "ポイント滞在",
    desc: "-5〜105% を 1% 刻み・上り+下り 221 点 (約 8 分)",
  },
  {
    type: 2,
    label: "リリース",
    desc: "-5〜105% の 1% 刻み 111 点から保持→コースト (約 6 分)",
  },
  {
    type: 3,
    label: "ステップ",
    desc: "0〜100% の 10% 刻み 110 ペア (約 7 分)。2 ゲインセット検証は PID 変更後にもう一周",
  },
] as const;

type Experiment = (typeof EXPERIMENTS)[number];

interface Props {
  addLog: (msg: string) => void;
  logs: LogEntry[];
}

export function ExperimentPanel({ addLog, logs }: Props) {
  const [confirm, setConfirm] = useState<Experiment | null>(null);
  const [pending, setPending] = useState(false);

  // 実行中判定: 最新の "EXP ..." debug メッセージから導出 (専用テレメトリなし, Q9)
  const running = useMemo(() => {
    for (let i = logs.length - 1; i >= 0; i--) {
      const m = logs[i].msg;
      if (!m.startsWith("EXP ")) continue;
      if (m.startsWith("EXP start") || m.startsWith("EXP progress")) return true;
      return false; // done / stop / abort はいずれも終了
    }
    return false;
  }, [logs]);

  async function start(exp: Experiment) {
    setConfirm(null);
    setPending(true);
    try {
      const resp = await protocol.sendCommand("start_experiment", { type: exp.type });
      if (!resp.ok) {
        addLog(
          `実験開始が拒否されました (${exp.label})。前提条件を確認: ` +
            "ETC 稼働中 (SIG_IN=HIGH・モーター ON) / plausibility 正常 (未ラッチ) / エンジン停止 / 車両静止 / " +
            "SD カード挿入 / モードノブ≠MOTOR_OFF",
        );
      }
    } catch (err) {
      addLog("Experiment start error: " + (err as Error).message);
    } finally {
      setPending(false);
    }
  }

  async function stop() {
    try {
      await protocol.sendCommand("stop_experiment");
    } catch (err) {
      addLog("Experiment stop error: " + (err as Error).message);
    }
  }

  return (
    <section class="panel">
      <h2>同定実験 {running && <span style={{ color: "var(--warn)" }}>実行中</span>}</h2>
      <div style={{ display: "flex", flexDirection: "column", gap: "6px" }}>
        {EXPERIMENTS.map((exp) => (
          <div key={exp.type} style={{ display: "flex", alignItems: "center", gap: "8px" }}>
            <button disabled={running || pending} onClick={() => setConfirm(exp)}>
              {exp.label}
            </button>
            <span style={{ color: "var(--text-dim)", fontSize: "0.8em" }}>{exp.desc}</span>
          </div>
        ))}
        <div>
          <button class="danger" disabled={!running} onClick={stop}>
            停止
          </button>
        </div>
      </div>

      {confirm && (
        <div
          style={{
            position: "fixed", inset: 0, zIndex: 1000,
            background: "rgba(0,0,0,0.6)",
            display: "flex", alignItems: "center", justifyContent: "center",
          }}
          onClick={() => setConfirm(null)}
        >
          <div
            style={{
              background: "var(--surface)", color: "var(--text)",
              border: "1px solid var(--border)", borderRadius: "10px",
              padding: "20px 22px", maxWidth: "440px", margin: "16px",
              boxShadow: "0 8px 32px rgba(0,0,0,0.5)",
            }}
            onClick={(e) => e.stopPropagation()}
          >
            <h2 style={{ marginTop: 0 }}>{confirm.label} を開始しますか?</h2>
            <p style={{ color: "var(--text-dim)" }}>{confirm.desc}</p>
            <p>
              スロットルが自動で動きます。エンジン停止・車両静止・SD カード挿入を
              確認してください。実行中は console の「停止」でいつでも中断できます。
            </p>
            <div style={{ display: "flex", gap: "8px", justifyContent: "flex-end", marginTop: "18px" }}>
              <button onClick={() => setConfirm(null)}>キャンセル</button>
              <button class="danger" onClick={() => start(confirm)}>開始</button>
            </div>
          </div>
        </div>
      )}
    </section>
  );
}
