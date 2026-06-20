import { useState, useEffect } from "preact/hooks";
import type { DeviceConfig, AutoShiftConfigT } from "../types";
import { protocol } from "../protocol";

interface Props {
  config: DeviceConfig | null;
  addLog: (msg: string) => void;
  onDirty: () => void;
  onUpdate: (a: AutoShiftConfigT) => void;
}

const FIELDS: { key: keyof AutoShiftConfigT; label: string; step: number }[] = [
  { key: "upshiftRpm", label: "Upshift RPM", step: 100 },
  { key: "downshiftRpm", label: "Downshift RPM", step: 100 },
  { key: "minWheelHz", label: "Min Wheel Hz", step: 0.5 },
  { key: "throttleOnPct", label: "Throttle On %", step: 1 },
  { key: "cooldownMs", label: "Cooldown ms", step: 10 },
  { key: "istPulseMs", label: "IST Pulse ms", step: 1 },
  { key: "normalDrivePulseMs", label: "NORMAL Drive ms", step: 5 },
  { key: "normalNeutralPulseMs", label: "NORMAL Neutral ms", step: 5 },
];

const INT_KEYS = new Set<keyof AutoShiftConfigT>([
  "cooldownMs",
  "istPulseMs",
  "normalDrivePulseMs",
  "normalNeutralPulseMs",
]);

export function AutoShiftTuner({ config, addLog, onDirty, onUpdate }: Props) {
  const [vals, setVals] = useState<Record<string, string>>({});

  useEffect(() => {
    if (config?.autoShift) {
      const a = config.autoShift;
      setVals(Object.fromEntries(FIELDS.map((f) => [f.key, String(a[f.key] ?? 0)])));
    }
  }, [config?.autoShift]);

  async function apply() {
    const out = {} as AutoShiftConfigT;
    for (const f of FIELDS) {
      const n = parseFloat(vals[f.key]);
      if (Number.isNaN(n)) {
        addLog(`AutoShift: invalid ${f.label}`);
        return;
      }
      (out[f.key] as number) = INT_KEYS.has(f.key) ? Math.round(n) : n;
    }
    try {
      const resp = await protocol.sendCommand("set_auto_shift", out as unknown as Record<string, unknown>);
      if (resp.ok) {
        onUpdate(out);
        addLog("AutoShift config applied");
        onDirty();
      } else {
        addLog("AutoShift apply failed");
      }
    } catch (err) {
      addLog("AutoShift error: " + (err as Error).message);
    }
  }

  return (
    <section>
      <h2>Auto Shifter Config</h2>
      <div class="autoshift-grid">
        {FIELDS.map((f) => (
          <label class="pid-field" key={f.key}>
            <span>{f.label}</span>
            <input
              type="number"
              step={f.step}
              value={vals[f.key] ?? ""}
              onInput={(e) => setVals((p) => ({ ...p, [f.key]: (e.target as HTMLInputElement).value }))}
            />
          </label>
        ))}
      </div>
      <div class="controls-row">
        <button class="pid-apply" onClick={apply}>Apply & Save</button>
      </div>
    </section>
  );
}
