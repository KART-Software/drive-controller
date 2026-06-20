import { useState, useRef } from "preact/hooks";
import type { DeviceConfig } from "../types";
import { SensorMonitor } from "./SensorMonitor";
import { ErrorStatus } from "./ErrorStatus";
import { SensorChart } from "./SensorChart";
import { CorrelationCharts } from "./CorrelationCharts";
import { ConfigPanel } from "./ConfigPanel";
import { DebugLog, type LogEntry } from "./DebugLog";
import { BarGauges } from "./BarGauges";
import { RawBarGauges } from "./RawBarGauges";
import { PidTuner } from "./PidTuner";
import { TargetBoundTuner } from "./TargetBoundTuner";
import { TargetCurveTuner } from "./TargetCurveTuner";
import { CurvePreview } from "./CurvePreview";
import { ModeKnob } from "./ModeKnob";

type SetConfig = (
  next: DeviceConfig | null | ((prev: DeviceConfig | null) => DeviceConfig | null),
) => void;

interface Props {
  config: DeviceConfig | null;
  setConfig: SetConfig;
  addLog: (msg: string, ts?: number) => void;
  setDirty: (v: boolean) => void;
  logs: LogEntry[];
}

export function EtcView({ config, setConfig, addLog, setDirty, logs }: Props) {
  const [timeRange, setTimeRange] = useState<[number, number] | null>(null);
  const timeRangeRef = useRef(timeRange);
  const [hoverTime, setHoverTime] = useState<number | null>(null);
  const [previewCurve, setPreviewCurve] = useState<{ a4: number; a3: number; a2: number; a1: number } | null>(null);

  return (
    <main>
      <div class="area-sensors"><SensorMonitor /></div>
      <div class="area-mode">
        <ModeKnob />
      </div>
      <div class="area-errors"><ErrorStatus flags={config?.plausibilityFlags ?? {}} addLog={addLog} onFlagsUpdate={(pf) => {
        setConfig((prev) => prev ? { ...prev, plausibilityFlags: pf } : prev);
      }} onDirty={() => setDirty(true)} /></div>
      <div class="area-gauges">
        <div class="gauges-col"><BarGauges addLog={addLog} onDirty={() => setDirty(true)} /></div>
        <div class="gauges-col"><RawBarGauges config={config} addLog={addLog} onConfigUpdate={(partial) => {
          setConfig((prev) => prev ? { ...prev, sensorValues: { ...prev.sensorValues, ...partial } } : prev);
        }} onDirty={() => setDirty(true)} />
          <PidTuner config={config} addLog={addLog} onDirty={() => setDirty(true)} onPidUpdate={(pid) => {
            setConfig((prev) => prev ? { ...prev, pid } : prev);
          }} />
          <TargetBoundTuner config={config} addLog={addLog} onDirty={() => setDirty(true)} onBoundUpdate={(bound) => {
            setConfig((prev) => prev ? { ...prev, sensorValues: { ...prev.sensorValues, ...bound } } : prev);
          }} />
        </div>
      </div>
      <div class="area-chart"><SensorChart onTimeRange={(min, max) => {
        const prev = timeRangeRef.current;
        if (!prev || Math.abs(prev[0] - min) > 0.05 || Math.abs(prev[1] - max) > 0.05) {
          const next: [number, number] = [min, max];
          timeRangeRef.current = next;
          setTimeRange(next);
        }
      }} hoverTime={hoverTime} /></div>
      <div class="area-corr"><CorrelationCharts timeRange={timeRange} onHoverTime={setHoverTime}
        targetCurve={previewCurve ?? config?.targetCurve} idling={config?.sensorValues.idling} normalMax={config?.sensorValues.normalMax} restrictedMax={config?.sensorValues.restrictedMax}
        curvePreview={<CurvePreview targetCurve={previewCurve ?? config?.targetCurve} />} footer={
        <TargetCurveTuner config={config} addLog={addLog} onDirty={() => setDirty(true)} onPreview={setPreviewCurve} onCurveUpdate={(targetCurve) => {
          setPreviewCurve(null);
          setConfig((prev) => prev ? { ...prev, targetCurve } : prev);
        }} />
      } /></div>
      <div class="area-config">
        <ConfigPanel config={config} onConfigLoaded={setConfig} addLog={addLog} />
      </div>
      <div class="area-log"><DebugLog entries={logs} /></div>
    </main>
  );
}
