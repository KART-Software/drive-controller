import { useState, useEffect } from "preact/hooks";
import type { DeviceConfig, SensorData } from "../types";
import { protocol } from "../protocol";
import { sensorStore } from "../sensor-store";

interface Props {
  config: DeviceConfig | null;
  addLog: (msg: string) => void;
  onConfigUpdate: (cfg: DeviceConfig) => void;
  onDirty: () => void;
}

export function ClutchCalibration({ config, addLog, onConfigUpdate, onDirty }: Props) {
  const [live, setLive] = useState<SensorData | null>(null);
  useEffect(() => {
    const iv = setInterval(() => {
      const d = sensorStore.latest;
      if (d) setLive(d);
    }, 100);
    return () => clearInterval(iv);
  }, []);

  async function capture(which: "min" | "max") {
    try {
      const resp = await protocol.sendCommand(which === "min" ? "set_clutch_min" : "set_clutch_max");
      if (resp.ok && resp.data) {
        onConfigUpdate(resp.data as unknown as DeviceConfig);
        addLog(`Clutch ${which} captured`);
        onDirty();
      } else {
        addLog("Clutch calib failed");
      }
    } catch (err) {
      addLog("Clutch calib error: " + (err as Error).message);
    }
  }

  return (
    <section>
      <h2>Clutch Calibration</h2>
      <p class="calib-hint">
        現在: <b>{live?.clutch != null ? live.clutch.toFixed(1) : "-"}%</b>
        {"　"}raw: <b>{live?.clutchRaw ?? "-"}</b>
      </p>
      <p class="calib-hint" style={{ color: "var(--text-dim)" }}>
        min: <b>{config?.sensorValues.clutchMin ?? "-"}</b>
        {"　"}max: <b>{config?.sensorValues.clutchMax ?? "-"}</b>
      </p>
      <div class="controls-row">
        <button onClick={() => capture("min")}>Set Clutch Min</button>
        <button onClick={() => capture("max")}>Set Clutch Max</button>
      </div>
    </section>
  );
}
