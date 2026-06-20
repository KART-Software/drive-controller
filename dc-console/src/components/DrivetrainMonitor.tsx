import { useState, useEffect } from "preact/hooks";
import type { SensorData } from "../types";
import { sensorStore } from "../sensor-store";

function fmt(v: number | undefined | null, d = 1): string {
  return v != null ? v.toFixed(d) : "-";
}

function gearLabel(g: number | undefined): string {
  if (g == null || g < 0) return "?";
  if (g === 0) return "N";
  return String(g);
}

function Row({ label, raw, val, unit }: { label: string; raw?: number; val?: number; unit: string }) {
  return (
    <div class="sensor-row">
      <span class="label">{label}</span>
      {raw != null && <span class="raw">{raw}</span>}
      <span class="val">{fmt(val)}</span>
      <span class="unit">{unit}</span>
    </div>
  );
}

export function DrivetrainMonitor() {
  const [data, setData] = useState<SensorData | null>(null);
  useEffect(() => {
    const iv = setInterval(() => {
      const d = sensorStore.latest;
      if (d) setData(d);
    }, 100);
    return () => clearInterval(iv);
  }, []);

  return (
    <section>
      <h2>Drivetrain Monitor</h2>
      <div class="sensor-grid">
        <div class="sensor-grid-row">
          <div class="sensor-group">
            <h3>Gear</h3>
            <div class="sensor-row">
              <span class="label">Gear</span>
              <span class="raw">{data?.gpsRaw ?? "-"}</span>
              <span class="val" style={{ fontSize: "1.1rem", fontWeight: 700 }}>{gearLabel(data?.gear)}</span>
              <span class="unit" />
            </div>
          </div>
          <div class="sensor-group">
            <h3>Clutch</h3>
            <Row label="Clutch" raw={data?.clutchRaw} val={data?.clutch} unit="%" />
          </div>
          <div class="sensor-group">
            <h3>Engine</h3>
            <Row label="RPM (Hz)" val={data?.rpm} unit="Hz" />
          </div>
        </div>
        <div class="sensor-grid-row">
          <div class="sensor-group">
            <h3>Wheel Speed</h3>
            <Row label="FL" val={data?.wheelFL} unit="Hz" />
            <Row label="FR" val={data?.wheelFR} unit="Hz" />
            <Row label="RL" val={data?.wheelRL} unit="Hz" />
            <Row label="RR" val={data?.wheelRR} unit="Hz" />
          </div>
          <div class="sensor-group">
            <h3>Accel</h3>
            <Row label="X" val={data?.ax} unit="m/s²" />
            <Row label="Y" val={data?.ay} unit="m/s²" />
            <Row label="Z" val={data?.az} unit="m/s²" />
          </div>
          <div class="sensor-group">
            <h3>Gyro</h3>
            <Row label="X" val={data?.gx} unit="dps" />
            <Row label="Y" val={data?.gy} unit="dps" />
            <Row label="Z" val={data?.gz} unit="dps" />
          </div>
        </div>
      </div>
    </section>
  );
}
