export interface SensorData {
  t: "s";
  ts: number;
  a1r: number;
  a2r: number;
  a1: number;
  a2: number;
  ir: number;
  i: number;
  t1r: number;
  t2r: number;
  t1: number;
  t2: number;
  br: number;
  b: number;
  tgt: number;
  m: string;
  manual: boolean;
  tgt_ittr: boolean;
  duty: number; // モーター実印加 duty [%] (±100, ETC 停止中 0)
  sys?: SysStatsT; // loop 停止の計測 (直近 1 s 窓)
  control?: ControlStatusT; // 制御入力の受信状態 (CAN 0x740 / 0x741 / override)
  v: boolean;
  err: number;
  sps?: number;

  // ── ドライブトレイン (非 ETC) ──
  gear?: number; // -1=不明, 0=N, 1-6
  gpsRaw?: number;
  clutch?: number; // 0-100%
  clutchRaw?: number;
  wheelFL?: number; // Hz
  wheelFR?: number;
  wheelRL?: number;
  wheelRR?: number;
  rpm?: number; // engine pulse Hz (歯数換算前)
  clutchRpm?: number; // クラッチ後(出力軸) Hz (歯数換算前)
  ax?: number;
  ay?: number;
  az?: number;
  gx?: number;
  gy?: number;
  gz?: number;
}

// 制御入力の受信状態 (docs/control_input_spec.md §3.4)
export interface ControlStatusT {
  source: "waiting" | "can" | "override";
  controlLinkAlive: boolean; // 0x740 を 200 ms 以内に受信
  shiftLinkAlive: boolean; // 0x741 を 100 ms 以内に受信
  controlRxAgeMs: number; // 最後に 0x740 を受信してからの経過 (未受信は 0)
  canEtcMode: string; // CAN 側の ETC モード ("" = 未受信)
  canAutoShift: boolean;
  autoShiftOn: boolean; // 適用中 (override 込み)
}

export interface SysStatsT {
  loopMaxUs: number;
  loopMeanUs: number;
  sdMaxUs: number;
  safetyMaxUs: number;
  logDrops: number;
  loopMaxUsBoot: number;
}

export interface DebugMessage {
  t: "d";
  ts: number;
  msg: string;
}

export interface ResponseMessage {
  t: "r";
  id: number;
  ok: boolean;
  data?: Record<string, unknown>;
}

export type Message = SensorData | DebugMessage | ResponseMessage;

export interface AutoShiftConfigT {
  upshiftRpm: number;
  downshiftRpm: number;
  minWheelHz: number;
  cooldownMs: number;
  istPulseMs: number;
  normalDrivePulseMs: number;
  normalNeutralPulseMs: number;
  throttleOnPct: number;
}

export interface DeviceConfig {
  sensorValues: {
    apps1Min: number;
    apps1Max: number;
    apps2Min: number;
    apps2Max: number;
    ittrMin: number;
    ittrMax: number;
    tps1Min: number;
    tps1Max: number;
    tps2Min: number;
    tps2Max: number;
    idling: number;
    normalMax: number;
    restrictedMax: number;
    clutchMin: number;
    clutchMax: number;
  };
  plausibilityFlags: Record<string, boolean>;
  useIttr: boolean;
  pid: { kP: number; kI: number; kD: number };
  targetCurve: { a4: number; a3: number; a2: number; a1: number };
  // ── 非 ETC ──
  gpsType: number; // 0=IST, 1=NORMAL
  autoShift: AutoShiftConfigT;
  // ── FS 使用量 (ConfigResponse 由来, 任意) ──
  fsUsed?: number;
  fsTotal?: number;
}
