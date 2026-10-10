import { useLatestSensor } from "../hooks/useSensorValue";
import { protocol } from "../protocol";
import { EtcMode } from "../proto/drive_controller_pb";
import type { ControlStatusT } from "../types";

// 制御入力 (コックピットスイッチ) の受信状態と、ベンチ用の上書き (docs/control_input_spec.md)。
// ETC モード / オートシフトは CAN 0x740 (data_logger) から来る。起動後に未受信の間は MOTOR_OFF で待機する。
// data_logger が無いベンチでは、ここから上書きして ETC を動かす。0x740 を受信している間は上書きできない
// (ファームが拒否し、受信が始まると自動で解除される)。
const MODES: { label: string; value: EtcMode }[] = [
  { label: "Calib", value: EtcMode.CALIB },
  { label: "Normal", value: EtcMode.NORMAL },
  { label: "Restrict", value: EtcMode.RESTRICT },
  { label: "Motor OFF", value: EtcMode.MOTOR_OFF },
];

const SOURCE_TEXT: Record<ControlStatusT["source"], string> = {
  waiting: "CAN 未受信 (MOTOR_OFF で待機)",
  can: "CAN",
  override: "console 上書き中",
};

interface Props {
  addLog: (msg: string) => void;
}

export function ControlInputPanel({ addLog }: Props) {
  const d = useLatestSensor(200);

  async function send(params: Record<string, unknown>, what: string) {
    try {
      const r = await protocol.sendCommand("set_control_override", params);
      if (!r.ok) addLog(`上書きが拒否されました (${what}): CAN 0x740 を受信中は上書きできません`);
    } catch (err) {
      addLog("Override error: " + (err as Error).message);
    }
  }

  if (!d?.control) {
    return (
      <section>
        <h2>Control Input</h2>
        <p style={{ color: "var(--text-dim)", fontSize: "0.85rem" }}>No data</p>
      </section>
    );
  }
  const c = d.control;
  const mode = d.m;
  const linkColor = c.controlLinkAlive ? "var(--ok)" : c.source === "waiting" ? "var(--warn)" : "var(--err)";
  const linkText = c.controlLinkAlive
    ? "0x740 受信中"
    : c.canEtcMode === ""
      ? "0x740 未受信"
      : `0x740 途絶 (${(c.controlRxAgeMs / 1000).toFixed(1)} s)`;
  const locked = c.controlLinkAlive;
  return (
    <section>
      <h2>Control Input</h2>
      <div style={{ fontSize: "0.85rem", display: "grid", gap: "4px" }}>
        <div>
          入力元: <b>{SOURCE_TEXT[c.source]}</b> / 適用中: <b>{mode || "-"}</b>, オートシフト{" "}
          <b>{c.autoShiftOn ? "ON" : "OFF"}</b>
        </div>
        <div style={{ color: linkColor }}>
          {linkText}
          {c.canEtcMode !== "" && ` — CAN 値: ${c.canEtcMode}, オートシフト ${c.canAutoShift ? "ON" : "OFF"}`}
          {" / "}
          {c.shiftLinkAlive ? "0x741 受信中" : "0x741 なし"}
        </div>
      </div>
      <div style={{ display: "flex", flexWrap: "wrap", gap: "6px", marginTop: "8px" }}>
        {MODES.map((m) => (
          <button
            key={m.value}
            disabled={locked}
            onClick={() => send({ enable: true, etcMode: m.value }, m.label)}
          >
            {m.label}
          </button>
        ))}
        <button disabled={locked} onClick={() => send({ enable: true, autoShift: !c.autoShiftOn }, "auto-shift")}>
          オートシフト {c.autoShiftOn ? "OFF" : "ON"}
        </button>
        <button disabled={c.source !== "override"} onClick={() => send({ enable: false }, "解除")}>
          上書き解除
        </button>
      </div>
    </section>
  );
}
