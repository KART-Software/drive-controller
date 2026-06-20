import { useState, useEffect } from "preact/hooks";
import type { DeviceConfig, SensorData } from "../types";
import { protocol } from "../protocol";
import { sensorStore } from "../sensor-store";

interface Props {
  config: DeviceConfig | null;
  addLog: (msg: string) => void;
  onDirty: () => void;
}

// シフトパターン (物理順)。gear id: 0=N, 1-6
const IST_GEARS = [0, 1, 2, 3, 4]; // N,1,2,3,4
const NORMAL_GEARS = [1, 0, 2, 3, 4, 5, 6]; // 1,N,2,3,4,5,6

function gearName(g: number): string {
  return g === 0 ? "N" : String(g);
}

export function GearCalibration({ config, addLog, onDirty }: Props) {
  const [live, setLive] = useState<SensorData | null>(null);
  const [rawByGear, setRawByGear] = useState<Record<number, number>>({});

  useEffect(() => {
    const iv = setInterval(() => {
      const d = sensorStore.latest;
      if (d) setLive(d);
    }, 100);
    return () => clearInterval(iv);
  }, []);

  const gears = config?.gpsType === 1 ? NORMAL_GEARS : IST_GEARS;

  async function calib(gear: number) {
    try {
      const resp = await protocol.sendCommand("set_gps_gear", { gear });
      if (resp.ok && resp.data) {
        const raws = resp.data.rawValues as number[] | undefined;
        const ids = resp.data.gears as number[] | undefined;
        if (raws && ids) {
          const map: Record<number, number> = {};
          ids.forEach((id, i) => (map[id] = raws[i]));
          setRawByGear(map);
        }
        addLog(`Gear ${gearName(gear)} calibrated`);
        onDirty();
      } else {
        addLog("Gear calib failed");
      }
    } catch (err) {
      addLog("Gear calib error: " + (err as Error).message);
    }
  }

  return (
    <section>
      <h2>Gear Calibration</h2>
      <p class="calib-hint">
        現在ギア: <b>{live?.gear != null && live.gear >= 0 ? gearName(live.gear) : "?"}</b>
        {"　"}raw: <b>{live?.gpsRaw ?? "-"}</b>
        {"　"}({config?.gpsType === 1 ? "NORMAL" : "IST"})
      </p>
      <p class="calib-hint" style={{ color: "var(--text-dim)" }}>
        各ギア位置にシフトした状態でボタンを押すと、現在の raw 値をそのギアとして登録します。
      </p>
      <div class="controls-row">
        {gears.map((g) => (
          <button key={g} onClick={() => calib(g)}>
            Set {gearName(g)}
            {rawByGear[g] != null && <span class="gear-raw"> ({rawByGear[g]})</span>}
          </button>
        ))}
      </div>
    </section>
  );
}
