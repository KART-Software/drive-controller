import type { DeviceConfig, AutoShiftConfigT } from "../types";
import { DrivetrainMonitor } from "./DrivetrainMonitor";
import { DrivetrainChart } from "./DrivetrainChart";
import { GearCalibration } from "./GearCalibration";
import { ClutchCalibration } from "./ClutchCalibration";
import { AutoShiftTuner } from "./AutoShiftTuner";
import { DebugLog, type LogEntry } from "./DebugLog";

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

export function DrivetrainView({ config, setConfig, addLog, setDirty, logs }: Props) {
  return (
    <main class="dt-main">
      <div class="dt-monitor"><DrivetrainMonitor /></div>
      <div class="dt-chart"><DrivetrainChart /></div>
      <div class="dt-calib">
        <GearCalibration config={config} addLog={addLog} onDirty={() => setDirty(true)} />
        <ClutchCalibration
          config={config}
          addLog={addLog}
          onConfigUpdate={(c) => setConfig(c)}
          onDirty={() => setDirty(true)}
        />
        <AutoShiftTuner
          config={config}
          addLog={addLog}
          onDirty={() => setDirty(true)}
          onUpdate={(a: AutoShiftConfigT) => setConfig((prev) => (prev ? { ...prev, autoShift: a } : prev))}
        />
      </div>
      <div class="dt-log"><DebugLog entries={logs} /></div>
    </main>
  );
}
