import { useState, useEffect } from "preact/hooks";
import type { DeviceConfig, RotationConfigT, RotationSensorConfigT } from "../types";
import { protocol } from "../protocol";

interface Props {
  config: DeviceConfig | null;
  addLog: (msg: string) => void;
  onDirty: () => void;
  onUpdate: (r: RotationConfigT) => void;
}

// 回転センサー (エンジン・クラッチ後・前輪・後輪) の設定。docs/loop_nonblocking_spec.md §6.7
type Row = "engine" | "clutch" | "wheelFront" | "wheelRear";
const ROWS: { key: Row; label: string; wheel: boolean }[] = [
  { key: "engine", label: "Engine", wheel: false },
  { key: "clutch", label: "Clutch out", wheel: false },
  { key: "wheelFront", label: "Wheel front", wheel: true },
  { key: "wheelRear", label: "Wheel rear", wheel: true },
];
const NUM_FIELDS: { key: keyof RotationSensorConfigT; label: string }[] = [
  { key: "teeth", label: "Teeth" },
  { key: "windowTeeth", label: "Window teeth" },
  { key: "maxWindowMs", label: "Max window ms" },
  { key: "staleMs", label: "Stale ms" },
];

function sensorOf(r: RotationConfigT, row: Row): RotationSensorConfigT {
  return row === "engine" || row === "clutch" ? r[row] : r[row].sensor;
}

export function RotationTuner({ config, addLog, onDirty, onUpdate }: Props) {
  const [draft, setDraft] = useState<RotationConfigT | null>(null);

  useEffect(() => {
    if (config?.rotation) setDraft(structuredClone(config.rotation));
  }, [config?.rotation]);

  function setSensor(row: Row, key: keyof RotationSensorConfigT, value: number | boolean) {
    setDraft((d) => {
      if (!d) return d;
      const next = structuredClone(d);
      (sensorOf(next, row)[key] as number | boolean) = value;
      return next;
    });
  }

  function setTire(row: "wheelFront" | "wheelRear", value: number) {
    setDraft((d) => {
      if (!d) return d;
      const next = structuredClone(d);
      next[row].tireDiameterMm = value;
      return next;
    });
  }

  async function apply() {
    if (!draft) return;
    for (const r of ROWS) {
      const s = sensorOf(draft, r.key);
      for (const f of NUM_FIELDS) {
        const v = s[f.key] as number;
        if (!Number.isFinite(v) || v < 0 || (f.key !== "staleMs" && f.key !== "maxWindowMs" && v < 1)) {
          addLog(`Rotation: invalid ${r.label} ${f.label}`);
          return;
        }
      }
    }
    try {
      const resp = await protocol.sendCommand("set_rotation", { rotation: draft });
      if (resp.ok) {
        onUpdate(draft);
        addLog("Rotation config applied");
        onDirty();
      } else {
        addLog("Rotation apply failed");
      }
    } catch (err) {
      addLog("Rotation error: " + (err as Error).message);
    }
  }

  if (!draft) return null;
  return (
    <section>
      <h2>Rotation Sensors</h2>
      <table class="rotation-table">
        <thead>
          <tr>
            <th></th>
            {NUM_FIELDS.map((f) => (
              <th key={f.key}>{f.label}</th>
            ))}
            <th>Both edges</th>
            <th>Tire Ø mm</th>
          </tr>
        </thead>
        <tbody>
          {ROWS.map((r) => {
            const s = sensorOf(draft, r.key);
            return (
              <tr key={r.key}>
                <td>{r.label}</td>
                {NUM_FIELDS.map((f) => (
                  <td key={f.key}>
                    <input
                      type="number"
                      step={1}
                      value={s[f.key] as number}
                      onInput={(e) => setSensor(r.key, f.key, Math.round(parseFloat((e.target as HTMLInputElement).value)))}
                    />
                  </td>
                ))}
                <td>
                  <input
                    type="checkbox"
                    checked={s.bothEdges}
                    onChange={(e) => setSensor(r.key, "bothEdges", (e.target as HTMLInputElement).checked)}
                  />
                </td>
                <td>
                  {r.wheel ? (
                    <input
                      type="number"
                      step={1}
                      value={draft[r.key as "wheelFront" | "wheelRear"].tireDiameterMm}
                      onInput={(e) =>
                        setTire(r.key as "wheelFront" | "wheelRear", parseFloat((e.target as HTMLInputElement).value))
                      }
                    />
                  ) : null}
                </td>
              </tr>
            );
          })}
        </tbody>
      </table>
      <div class="controls-row">
        <button class="pid-apply" onClick={apply}>Apply & Save</button>
      </div>
    </section>
  );
}
