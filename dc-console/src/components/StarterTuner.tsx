import { useState, useEffect } from "preact/hooks";
import type { DeviceConfig, StarterConfigT } from "../types";
import { protocol } from "../protocol";

interface Props {
  config: DeviceConfig | null;
  addLog: (msg: string) => void;
  onDirty: () => void;
  onUpdate: (s: StarterConfigT) => void;
}

// セルモーターリレー (docs/starter_spec.md)。出力ピンは firmware の constants.hpp
const FIELDS: { key: keyof StarterConfigT; label: string; step: number }[] = [
  { key: "engineRunningRpm", label: "Engine running RPM", step: 100 },
  { key: "runningConfirmMs", label: "Running confirm ms", step: 100 },
  { key: "minHoldMs", label: "Min hold ms", step: 500 },
];

const INT_KEYS = new Set<keyof StarterConfigT>(["runningConfirmMs", "minHoldMs"]);

export function StarterTuner({ config, addLog, onDirty, onUpdate }: Props) {
  const [vals, setVals] = useState<Record<string, string>>({});

  useEffect(() => {
    if (config?.starter) {
      const s = config.starter;
      setVals(Object.fromEntries(FIELDS.map((f) => [f.key, String(s[f.key] ?? 0)])));
    }
  }, [config?.starter]);

  async function apply() {
    const out = {} as StarterConfigT;
    for (const f of FIELDS) {
      const n = parseFloat(vals[f.key]);
      if (Number.isNaN(n) || n < 0) {
        addLog(`Starter: invalid ${f.label}`);
        return;
      }
      (out[f.key] as number) = INT_KEYS.has(f.key) ? Math.round(n) : n;
    }
    try {
      const resp = await protocol.sendCommand("set_starter", out as unknown as Record<string, unknown>);
      if (resp.ok) {
        onUpdate(out);
        addLog("Starter config applied");
        onDirty();
      } else {
        addLog("Starter apply failed");
      }
    } catch (err) {
      addLog("Starter error: " + (err as Error).message);
    }
  }

  return (
    <section>
      <h2>Starter Config</h2>
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
