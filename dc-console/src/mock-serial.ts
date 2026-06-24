import type { Transport } from "./transport";
import { create, toBinary, fromBinary } from "@bufbuild/protobuf";
import {
  DeviceToHostSchema,
  HostToDeviceSchema,
  ResponseSchema,
  StateSchema,
  SensorSchema,
  EtcStateSchema,
  ConfigSchema,
  EtcMode,
} from "./proto/drive_controller_pb";
import type {
  Command,
  DeviceToHost,
  Response,
  Config as PbConfig,
} from "./proto/drive_controller_pb";
import { FrameDecoder } from "./serial/cobs";
import { crc16Ccitt } from "./serial/crc16";
import { cobsCrcEncode } from "./serial/cobs-crc";

const SENSOR_INTERVAL = 20; // 50Hz

let connected = false;
let timer: ReturnType<typeof setInterval> | null = null;
let onBytes: ((chunk: Uint8Array) => void) | null = null;
let onDisconnect: (() => void) | null = null;

let t0 = 0;
let manualMode = false;
let manualTarget = 30;
let configChanged = false;
const MOCK_MODES = [EtcMode.CALIB, EtcMode.NORMAL, EtcMode.RESTRICT, EtcMode.MOTOR_OFF] as const;
const flags = {
  apps: true,
  tps: true,
  apps1: true,
  apps2: true,
  tps1: true,
  tps2: true,
  target: true,
  bps: true,
  bpsTps: true,
};
let useIttr = false;
const sensorValues = {
  apps1Min: 200,
  apps1Max: 3800,
  apps2Min: 200,
  apps2Max: 3800,
  ittrMin: 100,
  ittrMax: 4000,
  tps1Min: 300,
  tps1Max: 3600,
  tps2Min: 300,
  tps2Max: 3600,
  idling: 5.0,
  normalMax: 100,
  restrictedMax: 60,
};
const pidGains = { kP: 3.0, kI: 0.4, kD: 0.0 };
const targetCurve = { a4: 0, a3: 0, a2: 0.0087, a1: 0.13 };
const clutchCalib = { min: 5000, max: 60000 };
let gpsType = 0; // 0=IST, 1=NORMAL
const gpsRawValues = [32768, 32768, 32768, 32768, 32768]; // IST: N,1,2,3,4
const autoShift = {
  upshiftRpm: 11000,
  downshiftRpm: 6000,
  minWheelHz: 5,
  cooldownMs: 250,
  istPulseMs: 15,
  normalDrivePulseMs: 100,
  normalNeutralPulseMs: 25,
  throttleOnPct: 50,
};

function getFullConfig(): PbConfig {
  return create(ConfigSchema, {
    sensorCalib: {
      apps1Min: sensorValues.apps1Min,
      apps1Max: sensorValues.apps1Max,
      apps2Min: sensorValues.apps2Min,
      apps2Max: sensorValues.apps2Max,
      ittrMin: sensorValues.ittrMin,
      ittrMax: sensorValues.ittrMax,
      tps1Min: sensorValues.tps1Min,
      tps1Max: sensorValues.tps1Max,
      tps2Min: sensorValues.tps2Min,
      tps2Max: sensorValues.tps2Max,
      targetTpIdling: sensorValues.idling,
      targetTpNormalMax: sensorValues.normalMax,
      targetTpRestrictedMax: sensorValues.restrictedMax,
      clutchMin: clutchCalib.min,
      clutchMax: clutchCalib.max,
      gps: { type: gpsType, istRawValues: [...gpsRawValues] },
    },
    etc: {
      plausibilityCheckFlags: { ...flags },
      useIttr,
      pid: { ...pidGains },
      targetCurve: { ...targetCurve },
    },
    autoShift: { ...autoShift },
  });
}

let mockFsTotal = 262144;
let mockFsUsed = 250000; // 初期を満杯近くにして FS フルダイアログを mock で確認できるように

function configRespValue() {
  return {
    config: getFullConfig(),
    changed: configChanged,
    fsUsed: mockFsUsed,
    fsTotal: mockFsTotal,
  };
}

function emitFrame(env: DeviceToHost): void {
  const pb = toBinary(DeviceToHostSchema, env);
  onBytes?.(cobsCrcEncode(pb));
}

function emitResponse(r: Response): void {
  emitFrame(
    create(DeviceToHostSchema, { payload: { case: "response", value: r } }),
  );
}

function sensorTick() {
  const elapsed = (Date.now() - t0) / 1000;
  const base = Math.sin(elapsed * 0.5) * 40 + 50;
  const noise = () => (Math.random() - 0.5) * 2;
  const a1 = Math.max(0, Math.min(100, base + noise()));
  const a2 = Math.max(0, Math.min(100, base + noise() + 0.5));
  const tgt = manualMode ? manualTarget : base;
  const t1 = Math.max(
    0,
    Math.min(100, tgt + noise() * 3 + Math.sin(elapsed * 2) * 2),
  );
  const t2 = Math.max(
    0,
    Math.min(100, tgt + noise() * 3 + Math.sin(elapsed * 2) * 2 + 0.3),
  );
  const ittr = base * 0.8 + noise();
  const bpsVal = 14.7 + Math.sin(elapsed * 0.3) * 2 + noise() * 0.5;

  // ── 非 ETC モック ──
  const speed = Math.max(0, Math.sin(elapsed * 0.25) * 30 + 30); // 車輪 Hz
  const rpmHz = Math.max(0, base * 1.2 + 20 + noise()); // engine pulse Hz
  const mockGear = (Math.floor(elapsed / 4) % 5); // 0..4 を巡回
  const sensor = create(SensorSchema, {
    sps: 50,
    apps1Raw: Math.round(a1 * 36 + 200),
    apps2Raw: Math.round(a2 * 36 + 200),
    ittrRaw: Math.round(ittr * 39 + 100),
    tps1Raw: Math.round(t1 * 33 + 300),
    tps2Raw: Math.round(t2 * 33 + 300),
    bpsRaw: Math.round(bpsVal * 100),
    apps1: +a1.toFixed(2),
    apps2: +a2.toFixed(2),
    ittr: +ittr.toFixed(2),
    tps1: +t1.toFixed(2),
    tps2: +t2.toFixed(2),
    bps: +bpsVal.toFixed(2),
    targetTp: +tgt.toFixed(2),
    wheelSpeedFl: +(speed + noise()).toFixed(2),
    wheelSpeedFr: +(speed + noise()).toFixed(2),
    wheelSpeedRl: +(speed + noise() + 1).toFixed(2),
    wheelSpeedRr: +(speed + noise() + 1).toFixed(2),
    rpm: +rpmHz.toFixed(2),
    gear: mockGear,
    gpsRaw: gpsRawValues[mockGear] ?? 32768,
    clutch: +Math.max(0, Math.min(100, base)).toFixed(1),
    clutchRaw: Math.round(clutchCalib.min + (base / 100) * (clutchCalib.max - clutchCalib.min)),
    clutchRpm: +(rpmHz * 0.9 + noise()).toFixed(2),
    accelX: +(noise() * 0.5).toFixed(2),
    accelY: +(noise() * 0.5).toFixed(2),
    accelZ: +(9.8 + noise() * 0.2).toFixed(2),
    gyroX: +(noise() * 5).toFixed(2),
    gyroY: +(noise() * 5).toFixed(2),
    gyroZ: +(Math.sin(elapsed) * 20).toFixed(2),
  });
  const etc = create(EtcStateSchema, {
    mode: MOCK_MODES[Math.floor(elapsed / 3) % MOCK_MODES.length],
    manual: manualMode,
    ittr: useIttr,
    valid: true,
    errors: 0,
  });
  const state = create(StateSchema, {
    timestamp: Date.now() - t0,
    sensor,
    etc,
  });
  emitFrame(
    create(DeviceToHostSchema, { payload: { case: "sensor", value: state } }),
  );
}

function handleCommand(cmd: Command): void {
  const id = cmd.id;
  const body = cmd.body;
  if (!body.case) {
    emitResponse(create(ResponseSchema, { id, ok: false }));
    return;
  }
  setTimeout(() => {
    switch (body.case) {
      case "getConfig":
        emitResponse(
          create(ResponseSchema, {
            id,
            ok: true,
            data: { case: "config", value: configRespValue() },
          }),
        );
        break;
      case "save":
      case "revert":
        configChanged = false;
        emitResponse(
          create(ResponseSchema, {
            id,
            ok: true,
            data: { case: "config", value: configRespValue() },
          }),
        );
        break;
      case "setRtc":
        emitResponse(create(ResponseSchema, { id, ok: true }));
        break;
      case "formatFs":
        mockFsUsed = 700; // フォーマットで空き容量回復、設定は保持
        configChanged = false;
        emitResponse(
          create(ResponseSchema, {
            id,
            ok: true,
            data: { case: "config", value: configRespValue() },
          }),
        );
        break;
      case "setPlausibilityFlags": {
        const d = body.value;
        if (d.apps !== undefined) flags.apps = d.apps;
        if (d.tps !== undefined) flags.tps = d.tps;
        if (d.apps1 !== undefined) flags.apps1 = d.apps1;
        if (d.apps2 !== undefined) flags.apps2 = d.apps2;
        if (d.tps1 !== undefined) flags.tps1 = d.tps1;
        if (d.tps2 !== undefined) flags.tps2 = d.tps2;
        if (d.target !== undefined) flags.target = d.target;
        if (d.bps !== undefined) flags.bps = d.bps;
        if (d.bpsTps !== undefined) flags.bpsTps = d.bpsTps;
        configChanged = true;
        emitResponse(
          create(ResponseSchema, {
            id,
            ok: true,
            data: {
              case: "plausibilityFlags",
              value: { ...flags },
            },
          }),
        );
        break;
      }
      case "setIttr":
        useIttr = body.value.use;
        configChanged = true;
        emitResponse(
          create(ResponseSchema, {
            id,
            ok: true,
            data: { case: "ittr", value: { use: useIttr } },
          }),
        );
        break;
      case "setEtcPid": {
        const d = body.value;
        if (d.kP !== undefined) pidGains.kP = d.kP;
        if (d.kI !== undefined) pidGains.kI = d.kI;
        if (d.kD !== undefined) pidGains.kD = d.kD;
        configChanged = true;
        emitResponse(
          create(ResponseSchema, {
            id,
            ok: true,
            data: { case: "pid", value: { ...pidGains } },
          }),
        );
        break;
      }
      case "setEtcTargetCurve": {
        const d = body.value;
        if (d.a4 !== undefined) targetCurve.a4 = d.a4;
        if (d.a3 !== undefined) targetCurve.a3 = d.a3;
        if (d.a2 !== undefined) targetCurve.a2 = d.a2;
        if (d.a1 !== undefined) targetCurve.a1 = d.a1;
        configChanged = true;
        emitResponse(
          create(ResponseSchema, {
            id,
            ok: true,
            data: { case: "targetCurve", value: { ...targetCurve } },
          }),
        );
        break;
      }
      case "setEtcManual":
        manualMode = !manualMode;
        if (manualMode) manualTarget = 30;
        emitResponse(create(ResponseSchema, { id, ok: true }));
        break;
      case "etcManualAdjust":
        manualTarget = Math.max(
          -10,
          Math.min(110, manualTarget + body.value.amount),
        );
        emitResponse(create(ResponseSchema, { id, ok: true }));
        break;
      case "setAppsMin":
        sensorValues.apps1Min = 200 + Math.round(Math.random() * 50);
        sensorValues.apps2Min = 200 + Math.round(Math.random() * 50);
        sensorValues.ittrMin = 100 + Math.round(Math.random() * 50);
        configChanged = true;
        emitResponse(
          create(ResponseSchema, {
            id,
            ok: true,
            data: {
              case: "appsMin",
              value: {
                apps1Min: sensorValues.apps1Min,
                apps2Min: sensorValues.apps2Min,
                ittrMin: sensorValues.ittrMin,
              },
            },
          }),
        );
        break;
      case "setAppsMax":
        sensorValues.apps1Max = 3700 + Math.round(Math.random() * 200);
        sensorValues.apps2Max = 3700 + Math.round(Math.random() * 200);
        sensorValues.ittrMax = 3900 + Math.round(Math.random() * 200);
        configChanged = true;
        emitResponse(
          create(ResponseSchema, {
            id,
            ok: true,
            data: {
              case: "appsMax",
              value: {
                apps1Max: sensorValues.apps1Max,
                apps2Max: sensorValues.apps2Max,
                ittrMax: sensorValues.ittrMax,
              },
            },
          }),
        );
        break;
      case "setTpsMin":
        sensorValues.tps1Min = 300 + Math.round(Math.random() * 50);
        sensorValues.tps2Min = 300 + Math.round(Math.random() * 50);
        configChanged = true;
        emitResponse(
          create(ResponseSchema, {
            id,
            ok: true,
            data: {
              case: "tpsMin",
              value: {
                tps1Min: sensorValues.tps1Min,
                tps2Min: sensorValues.tps2Min,
              },
            },
          }),
        );
        break;
      case "setTpsMax":
        sensorValues.tps1Max = 3500 + Math.round(Math.random() * 200);
        sensorValues.tps2Max = 3500 + Math.round(Math.random() * 200);
        configChanged = true;
        emitResponse(
          create(ResponseSchema, {
            id,
            ok: true,
            data: {
              case: "tpsMax",
              value: {
                tps1Max: sensorValues.tps1Max,
                tps2Max: sensorValues.tps2Max,
              },
            },
          }),
        );
        break;
      case "setIdling":
        sensorValues.idling = +(4 + Math.random() * 3).toFixed(1);
        configChanged = true;
        emitResponse(
          create(ResponseSchema, {
            id,
            ok: true,
            data: {
              case: "idling",
              value: { idling: sensorValues.idling },
            },
          }),
        );
        break;
      case "setEtcTargetBound": {
        const d = body.value;
        if (d.idling !== undefined) sensorValues.idling = d.idling;
        if (d.normalMax !== undefined) sensorValues.normalMax = d.normalMax;
        if (d.restrictedMax !== undefined)
          sensorValues.restrictedMax = d.restrictedMax;
        configChanged = true;
        emitResponse(
          create(ResponseSchema, {
            id,
            ok: true,
            data: {
              case: "targetBound",
              value: {
                idling: sensorValues.idling,
                normalMax: sensorValues.normalMax,
                restrictedMax: sensorValues.restrictedMax,
              },
            },
          }),
        );
        break;
      }
      case "setConfig": {
        const cfg = body.value.config;
        if (cfg) {
          if (cfg.sensorCalib) {
            const sv = cfg.sensorCalib;
            sensorValues.apps1Min = sv.apps1Min ?? sensorValues.apps1Min;
            sensorValues.apps1Max = sv.apps1Max ?? sensorValues.apps1Max;
            sensorValues.apps2Min = sv.apps2Min ?? sensorValues.apps2Min;
            sensorValues.apps2Max = sv.apps2Max ?? sensorValues.apps2Max;
            sensorValues.ittrMin = sv.ittrMin ?? sensorValues.ittrMin;
            sensorValues.ittrMax = sv.ittrMax ?? sensorValues.ittrMax;
            sensorValues.tps1Min = sv.tps1Min ?? sensorValues.tps1Min;
            sensorValues.tps1Max = sv.tps1Max ?? sensorValues.tps1Max;
            sensorValues.tps2Min = sv.tps2Min ?? sensorValues.tps2Min;
            sensorValues.tps2Max = sv.tps2Max ?? sensorValues.tps2Max;
            sensorValues.idling = sv.targetTpIdling ?? sensorValues.idling;
            sensorValues.normalMax =
              sv.targetTpNormalMax ?? sensorValues.normalMax;
            sensorValues.restrictedMax =
              sv.targetTpRestrictedMax ?? sensorValues.restrictedMax;
          }
          if (cfg.etc) {
            if (cfg.etc.plausibilityCheckFlags)
              Object.assign(flags, cfg.etc.plausibilityCheckFlags);
            if (cfg.etc.pid) Object.assign(pidGains, cfg.etc.pid);
            if (cfg.etc.targetCurve)
              Object.assign(targetCurve, cfg.etc.targetCurve);
            useIttr = cfg.etc.useIttr;
          }
          if (cfg.autoShift) Object.assign(autoShift, cfg.autoShift);
          configChanged = true;
        }
        emitResponse(create(ResponseSchema, { id, ok: !!cfg }));
        break;
      }
      case "setGpsGear": {
        const g = body.value.gear;
        gpsRawValues[g] = 30000 + Math.round(Math.random() * 5000);
        configChanged = true;
        emitResponse(
          create(ResponseSchema, {
            id,
            ok: true,
            data: {
              case: "gpsGear",
              value: { gear: g, rawValues: [...gpsRawValues], gears: [0, 1, 2, 3, 4] },
            },
          }),
        );
        break;
      }
      case "setClutchMin":
      case "setClutchMax": {
        const cur = 20000 + Math.round(Math.random() * 5000);
        if (body.case === "setClutchMin") clutchCalib.min = cur;
        else clutchCalib.max = cur;
        configChanged = true;
        emitResponse(
          create(ResponseSchema, {
            id,
            ok: true,
            data: { case: "config", value: configRespValue() },
          }),
        );
        break;
      }
      case "setTransmissionType": {
        gpsType = body.value.type;
        configChanged = true;
        emitResponse(
          create(ResponseSchema, {
            id,
            ok: true,
            data: { case: "config", value: configRespValue() },
          }),
        );
        break;
      }
      case "reboot":
      case "etcMotorOff":
      default:
        emitResponse(create(ResponseSchema, { id, ok: true }));
        break;
    }
  }, 5);
}

const decoder = new FrameDecoder((payload) => {
  if (payload.length < 2) return;
  const pbLen = payload.length - 2;
  const recvCrc = payload[pbLen] | (payload[pbLen + 1] << 8);
  const calcCrc = crc16Ccitt(payload.subarray(0, pbLen));
  if (recvCrc !== calcCrc) return;
  try {
    const env = fromBinary(HostToDeviceSchema, payload.subarray(0, pbLen));
    if (env.payload.case === "command") {
      handleCommand(env.payload.value);
    }
  } catch {
    // ignore malformed
  }
});

export const mockSerial: Transport = {
  async connect() {
    connected = true;
    t0 = Date.now();
    timer = setInterval(sensorTick, SENSOR_INTERVAL);
  },
  async disconnect() {
    connected = false;
    if (timer) {
      clearInterval(timer);
      timer = null;
    }
    onDisconnect?.();
  },
  async send(bytes: Uint8Array) {
    decoder.push(bytes);
  },
  isConnected() {
    return connected;
  },
  setOnBytes(fn) {
    onBytes = fn;
  },
  setOnDisconnect(fn) {
    onDisconnect = fn;
  },
};
