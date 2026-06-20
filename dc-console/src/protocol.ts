import { create, toBinary, fromBinary } from "@bufbuild/protobuf";
import {
  CommandSchema,
  HostToDeviceSchema,
  DeviceToHostSchema,
  ConfigSchema,
  EtcMode,
} from "./proto/drive_controller_pb";
import type {
  Command,
  HostToDevice,
  DeviceToHost,
  State as PbState,
  Config as PbConfig,
} from "./proto/drive_controller_pb";
import { FrameDecoder } from "./serial/cobs";
import { crc16Ccitt } from "./serial/crc16";
import { cobsCrcEncode } from "./serial/cobs-crc";
import type { Transport } from "./transport";
import type { SensorData, ResponseMessage, DeviceConfig } from "./types";

const RESPONSE_TIMEOUT = 3000;

let commandId = 0;
let transport: Transport | null = null;
const pending = new Map<
  number,
  {
    resolve: (msg: ResponseMessage) => void;
    reject: (err: Error) => void;
    timer: ReturnType<typeof setTimeout>;
  }
>();

let onSensorData: ((data: SensorData) => void) | null = null;
let onDebugLog: ((msg: string, ts: number) => void) | null = null;
let onSerialLog: ((direction: "rx" | "tx", line: string) => void) | null = null;

const decoder = new FrameDecoder(handleFramePayload);

function modeToString(m: EtcMode): string {
  switch (m) {
    case EtcMode.CALIB:
      return "Calib";
    case EtcMode.NORMAL:
      return "Normal";
    case EtcMode.RESTRICT:
      return "Restrict";
    case EtcMode.MOTOR_OFF:
      return "MotorOff";
    default:
      return "";
  }
}

function toAppSensor(st: PbState): SensorData {
  const s = st.sensor;
  const e = st.etc;
  return {
    t: "s",
    ts: st.timestamp,
    a1r: s?.apps1Raw ?? 0,
    a2r: s?.apps2Raw ?? 0,
    ir: s?.ittrRaw ?? 0,
    t1r: s?.tps1Raw ?? 0,
    t2r: s?.tps2Raw ?? 0,
    br: s?.bpsRaw ?? 0,
    a1: s?.apps1 ?? 0,
    a2: s?.apps2 ?? 0,
    i: s?.ittr ?? 0,
    t1: s?.tps1 ?? 0,
    t2: s?.tps2 ?? 0,
    b: s?.bps ?? 0,
    tgt: s?.targetTp ?? 0,
    m: modeToString(e?.mode ?? EtcMode.UNSPECIFIED),
    manual: e?.manual ?? false,
    tgt_ittr: false,
    v: e?.valid ?? false,
    err: e?.errors ?? 0,
    sps: s?.sps,
    // ── ドライブトレイン (非 ETC) ──
    gear: s?.gear,
    gpsRaw: s?.gpsRaw,
    clutch: s?.clutch,
    clutchRaw: s?.clutchRaw,
    wheelFL: s?.wheelSpeedFl,
    wheelFR: s?.wheelSpeedFr,
    wheelRL: s?.wheelSpeedRl,
    wheelRR: s?.wheelSpeedRr,
    rpm: s?.rpm,
    ax: s?.accelX,
    ay: s?.accelY,
    az: s?.accelZ,
    gx: s?.gyroX,
    gy: s?.gyroY,
    gz: s?.gyroZ,
  };
}

function handleFramePayload(payload: Uint8Array): void {
  if (payload.length < 2) return;
  const pbLen = payload.length - 2;
  const recvCrc = payload[pbLen] | (payload[pbLen + 1] << 8);
  const calcCrc = crc16Ccitt(payload.subarray(0, pbLen));
  if (recvCrc !== calcCrc) {
    onSerialLog?.("rx", `<crc-mismatch len=${pbLen}>`);
    return;
  }

  let env: DeviceToHost;
  try {
    env = fromBinary(DeviceToHostSchema, payload.subarray(0, pbLen));
  } catch (err) {
    onSerialLog?.("rx", `<decode-error: ${(err as Error).message}>`);
    return;
  }

  const p = env.payload;
  switch (p.case) {
    case "sensor":
      onSensorData?.(toAppSensor(p.value));
      break;
    case "debug":
      onDebugLog?.(p.value.msg, p.value.timestamp);
      onSerialLog?.("rx", `dbg: ${p.value.msg}`);
      break;
    case "response": {
      const r = p.value;
      const data = r.data;
      let normalized: Record<string, unknown> | undefined;
      if (data.case === "config") {
        // data.value は ConfigResponse { config, changed }
        normalized = {
          ...(toDeviceConfig(data.value.config) as unknown as Record<string, unknown>),
          configChanged: data.value.changed,
        };
      } else if (data.case) {
        normalized = data.value as unknown as Record<string, unknown>;
      }
      const resp: ResponseMessage = {
        t: "r",
        id: r.id,
        ok: r.ok,
        data: normalized,
      };
      onSerialLog?.("rx", `resp id=${r.id} ok=${r.ok}`);
      resolvePending(resp);
      break;
    }
  }
}

function resolvePending(msg: ResponseMessage): void {
  const entry = pending.get(msg.id);
  if (!entry) return;
  clearTimeout(entry.timer);
  pending.delete(msg.id);
  entry.resolve(msg);
}

function toDeviceConfig(cfg: PbConfig | undefined): DeviceConfig {
  const sv = cfg?.sensorCalib;
  const ec = cfg?.etc;
  const as = cfg?.autoShift;
  return {
    sensorValues: {
      apps1Min: sv?.apps1Min ?? 0,
      apps1Max: sv?.apps1Max ?? 0,
      apps2Min: sv?.apps2Min ?? 0,
      apps2Max: sv?.apps2Max ?? 0,
      ittrMin: sv?.ittrMin ?? 0,
      ittrMax: sv?.ittrMax ?? 0,
      tps1Min: sv?.tps1Min ?? 0,
      tps1Max: sv?.tps1Max ?? 0,
      tps2Min: sv?.tps2Min ?? 0,
      tps2Max: sv?.tps2Max ?? 0,
      idling: sv?.targetTpIdling ?? 0,
      normalMax: sv?.targetTpNormalMax ?? 0,
      restrictedMax: sv?.targetTpRestrictedMax ?? 0,
      clutchMin: sv?.clutchMin ?? 0,
      clutchMax: sv?.clutchMax ?? 0,
    },
    gpsType: sv?.gps?.type ?? 0,
    autoShift: {
      upshiftRpm: as?.upshiftRpm ?? 0,
      downshiftRpm: as?.downshiftRpm ?? 0,
      minWheelHz: as?.minWheelHz ?? 0,
      cooldownMs: as?.cooldownMs ?? 0,
      istPulseMs: as?.istPulseMs ?? 0,
      normalDrivePulseMs: as?.normalDrivePulseMs ?? 0,
      normalNeutralPulseMs: as?.normalNeutralPulseMs ?? 0,
      throttleOnPct: as?.throttleOnPct ?? 0,
    },
    plausibilityFlags: ec?.plausibilityCheckFlags
      ? {
          apps: ec.plausibilityCheckFlags.apps,
          tps: ec.plausibilityCheckFlags.tps,
          apps1: ec.plausibilityCheckFlags.apps1,
          apps2: ec.plausibilityCheckFlags.apps2,
          tps1: ec.plausibilityCheckFlags.tps1,
          tps2: ec.plausibilityCheckFlags.tps2,
          target: ec.plausibilityCheckFlags.target,
          bps: ec.plausibilityCheckFlags.bps,
          bpsTps: ec.plausibilityCheckFlags.bpsTps,
        }
      : {},
    useIttr: ec?.useIttr ?? false,
    pid: ec?.pid
      ? { kP: ec.pid.kP, kI: ec.pid.kI, kD: ec.pid.kD }
      : { kP: 0, kI: 0, kD: 0 },
    targetCurve: ec?.targetCurve
      ? {
          a4: ec.targetCurve.a4,
          a3: ec.targetCurve.a3,
          a2: ec.targetCurve.a2,
          a1: ec.targetCurve.a1,
        }
      : { a4: 0, a3: 0, a2: 0, a1: 0 },
  };
}

function buildConfigFromObject(obj: unknown): PbConfig | undefined {
  if (!obj || typeof obj !== "object") return undefined;
  const o = obj as Record<string, any>;
  const sv = o.sensorValues;
  const pf = o.plausibilityFlags;
  return create(ConfigSchema, {
    sensorCalib: sv
      ? {
          ...sv,
          targetTpIdling: sv.idling ?? sv.targetTpIdling ?? 0,
          targetTpNormalMax: sv.normalMax ?? sv.targetTpNormalMax ?? 0,
          targetTpRestrictedMax:
            sv.restrictedMax ?? sv.targetTpRestrictedMax ?? 0,
          clutchMin: sv.clutchMin ?? 0,
          clutchMax: sv.clutchMax ?? 0,
        }
      : undefined,
    etc: {
      plausibilityCheckFlags: pf ?? o.etc?.plausibilityCheckFlags,
      useIttr: o.useIttr ?? o.etc?.useIttr ?? false,
      pid: o.pid ?? o.etc?.pid,
      targetCurve: o.targetCurve ?? o.etc?.targetCurve,
    },
    autoShift: o.autoShift ?? undefined,
  });
}

function buildCommand(
  cmd: string,
  params: Record<string, any>,
  id: number,
): Command {
  switch (cmd) {
    case "motor_off":
      return create(CommandSchema, {
        id,
        body: { case: "etcMotorOff", value: {} },
      });
    case "save":
      return create(CommandSchema, { id, body: { case: "save", value: {} } });
    case "set_apps_min":
      return create(CommandSchema, {
        id,
        body: { case: "setAppsMin", value: {} },
      });
    case "set_apps_max":
      return create(CommandSchema, {
        id,
        body: { case: "setAppsMax", value: {} },
      });
    case "set_tps_min":
      return create(CommandSchema, {
        id,
        body: { case: "setTpsMin", value: {} },
      });
    case "set_tps_max":
      return create(CommandSchema, {
        id,
        body: { case: "setTpsMax", value: {} },
      });
    case "set_idling":
      return create(CommandSchema, {
        id,
        body: { case: "setIdling", value: {} },
      });
    case "set_target_bound":
      return create(CommandSchema, {
        id,
        body: {
          case: "setEtcTargetBound",
          value: {
            idling: params.idling,
            normalMax: params.normalMax,
            restrictedMax: params.restrictedMax,
          },
        },
      });
    case "set_plausibility_check_flags":
    case "set_plausibility_flags":
      return create(CommandSchema, {
        id,
        body: {
          case: "setPlausibilityFlags",
          value: {
            apps: params.apps,
            tps: params.tps,
            apps1: params.apps1,
            apps2: params.apps2,
            tps1: params.tps1,
            tps2: params.tps2,
            target: params.target,
            bps: params.bps,
            bpsTps: params.bpsTps ?? params.bps_tps,
          },
        },
      });
    case "set_ittr":
      return create(CommandSchema, {
        id,
        body: { case: "setIttr", value: { use: !!params.val } },
      });
    case "set_pid":
      return create(CommandSchema, {
        id,
        body: {
          case: "setEtcPid",
          value: { kP: params.kP, kI: params.kI, kD: params.kD },
        },
      });
    case "set_target_curve":
      return create(CommandSchema, {
        id,
        body: {
          case: "setEtcTargetCurve",
          value: { a4: params.a4, a3: params.a3, a2: params.a2, a1: params.a1 },
        },
      });
    case "set_manual":
      return create(CommandSchema, {
        id,
        body: { case: "setEtcManual", value: {} },
      });
    case "manual_adjust":
      return create(CommandSchema, {
        id,
        body: { case: "etcManualAdjust", value: { amount: params.amount } },
      });
    case "get_config":
      return create(CommandSchema, {
        id,
        body: { case: "getConfig", value: {} },
      });
    case "set_config": {
      let cfgObj: unknown = params.config;
      if (typeof cfgObj === "string") {
        try {
          cfgObj = JSON.parse(cfgObj);
        } catch {
          cfgObj = undefined;
        }
      }
      return create(CommandSchema, {
        id,
        body: {
          case: "setConfig",
          value: { config: buildConfigFromObject(cfgObj) },
        },
      });
    }
    case "reboot":
      return create(CommandSchema, { id, body: { case: "reboot", value: {} } });
    case "revert":
      return create(CommandSchema, { id, body: { case: "revert", value: {} } });
    case "set_gps_gear":
      return create(CommandSchema, {
        id,
        body: { case: "setGpsGear", value: { gear: params.gear } },
      });
    case "set_clutch_min":
      return create(CommandSchema, {
        id,
        body: { case: "setClutchMin", value: {} },
      });
    case "set_clutch_max":
      return create(CommandSchema, {
        id,
        body: { case: "setClutchMax", value: {} },
      });
    case "set_auto_shift":
      // auto_shift のみを設定した部分 Config を送る。firmware overlayConfig が
      // has_auto_shift だけを見て他セクションを温存する (CLAUDE.md の不変条件)。
      return create(CommandSchema, {
        id,
        body: {
          case: "setConfig",
          value: {
            config: create(ConfigSchema, {
              autoShift: {
                upshiftRpm: params.upshiftRpm,
                downshiftRpm: params.downshiftRpm,
                minWheelHz: params.minWheelHz,
                cooldownMs: params.cooldownMs,
                istPulseMs: params.istPulseMs,
                normalDrivePulseMs: params.normalDrivePulseMs,
                normalNeutralPulseMs: params.normalNeutralPulseMs,
                throttleOnPct: params.throttleOnPct,
              },
            }),
          },
        },
      });
    default:
      throw new Error(`Unknown command: ${cmd}`);
  }
}

function encodeFrame(env: HostToDevice): Uint8Array {
  const pb = toBinary(HostToDeviceSchema, env);
  return cobsCrcEncode(pb);
}

function sendCommand(
  cmd: string,
  params: Record<string, unknown> = {},
): Promise<ResponseMessage> {
  const id = ++commandId;
  const command = buildCommand(cmd, params as Record<string, any>, id);
  const env = create(HostToDeviceSchema, {
    payload: { case: "command", value: command },
  });

  return new Promise<ResponseMessage>((resolve, reject) => {
    const timer = setTimeout(() => {
      pending.delete(id);
      reject(new Error(`Command '${cmd}' timed out`));
    }, RESPONSE_TIMEOUT);

    pending.set(id, { resolve, reject, timer });
    try {
      const frame = encodeFrame(env);
      onSerialLog?.("tx", `${cmd} id=${id}`);
      transport?.send(frame);
    } catch (err) {
      clearTimeout(timer);
      pending.delete(id);
      reject(err as Error);
    }
  });
}

export const protocol = {
  sendCommand,
  setOnSensorData(fn: (data: SensorData) => void) {
    onSensorData = fn;
  },
  setOnDebugLog(fn: (msg: string, ts: number) => void) {
    onDebugLog = fn;
  },
  setOnSerialLog(fn: (direction: "rx" | "tx", line: string) => void) {
    onSerialLog = fn;
  },
  setTransport(t: Transport) {
    transport = t;
    decoder.reset();
    t.setOnBytes((chunk) => decoder.push(chunk));
  },
};
