// SD ログのフォーマット。firmware の dc-firmware/dc-main/src/util/log/log_record.hpp と tools/decode_log.py と揃える
// (docs/loop_nonblocking_spec.md §7.2、docs/log_viewer_spec.md)。レイアウトを変えるときは 3 か所。
export const MAGIC = "KARTLOG1";
export const VERSION = 0;
export const HEADER_SIZE = 32;
export const RECORD_SIZE = 176;
export const GAP_WARN_US = 1500; // これより t_us が飛んだら欠けとして数える (decode_log.py GAP_WARN_US)

const FLAG_NAMES = ["f_manual", "f_ittr", "f_shutdown", "f_valid", "f_autoshift", "f_launch", "f_starter_relay", "f_starter_switch"];

// CSV の列 (decode_log.py COLUMNS と同一)
export const COLUMNS: string[] = [
  "t_us", "t_s", "abs_time",
  ...Array.from({ length: 8 }, (_, i) => `adc${i}`),
  "apps1", "apps2", "ittr", "tps1", "tps2", "bps", "clutch",
  "ax", "ay", "az", "gx", "gy", "gz",
  "wheel_fl_hz", "wheel_fr_hz", "wheel_rl_hz", "wheel_rr_hz", "engine_hz", "clutch_hz",
  "wheel_fl_kmh", "wheel_fr_kmh", "wheel_rl_kmh", "wheel_rr_kmh", "engine_rpm", "clutch_rpm",
  "wc_fl", "wc_fr", "wc_rl", "wc_rr",
  "target_tp", "duty",
  "errors", "flags", ...FLAG_NAMES,
  "gear", "mode", "autoshift_state",
  "exp_type", "exp_index", "exp_phase",
  "loop_max_us", "sd_max_us", "tick_max_us", "log_drops",
  "can_fault", "can_tec", "can_rec",
];

export interface LogHeader {
  version: number;
  recordSize: number;
  logHz: number;
  dirNumber: number;
  fileIndex: number;
  rtcEpoch: number; // 0 = RTC 未設定
  rtcTUs: number;
}

export class LogFormatError extends Error {}

/** 先頭 32 B。短ければ null (作っただけで電源が切れた空ファイル)。magic / version / record_size が合わなければ LogFormatError */
export function parseHeader(buf: ArrayBuffer, name: string): LogHeader | null {
  if (buf.byteLength < HEADER_SIZE) return null;
  const v = new DataView(buf);
  const magic = String.fromCharCode(...new Uint8Array(buf, 0, 8));
  if (magic !== MAGIC) throw new LogFormatError(`${name}: ログファイルではない (magic ${JSON.stringify(magic)})`);
  const h: LogHeader = {
    version: v.getUint16(8, true),
    recordSize: v.getUint16(10, true),
    logHz: v.getUint16(12, true),
    dirNumber: v.getUint16(14, true),
    fileIndex: v.getUint16(16, true),
    rtcEpoch: v.getUint32(20, true),
    rtcTUs: Number(v.getBigUint64(24, true)),
  };
  if (h.version !== VERSION || h.recordSize !== RECORD_SIZE) {
    throw new LogFormatError(`${name}: version ${h.version} / record_size ${h.recordSize} B (このビューアは version ${VERSION} / ${RECORD_SIZE} B)`);
  }
  return h;
}

export function recordTUs(v: DataView, off: number): number {
  return Number(v.getBigUint64(off, true));
}

/** 全バイトがゼロ = 事前確保した未使用領域 (有効なレコードの t_us は 0 にならない) */
export function isZeroRecord(bytes: Uint8Array, off: number): boolean {
  for (let i = 0; i < RECORD_SIZE; i++) if (bytes[off + i] !== 0) return false;
  return true;
}

export function recordDrops(v: DataView, off: number): number {
  return v.getUint16(off + 146, true);
}

/** 1 レコード → CSV の 1 行分の値 (COLUMNS の順) */
export function recordToRow(v: DataView, off: number, h: LogHeader): (number | string)[] {
  const tUs = recordTUs(v, off);
  const f = (o: number) => v.getFloat32(off + o, true);
  const u32 = (o: number) => v.getUint32(off + o, true);
  const u16 = (o: number) => v.getUint16(off + o, true);
  const u8 = (o: number) => v.getUint8(off + o);
  const flags = u16(128);
  return [
    tUs, tUs / 1e6, h.rtcEpoch ? h.rtcEpoch + (tUs - h.rtcTUs) / 1e6 : "",
    u16(8), u16(10), u16(12), u16(14), u16(16), u16(18), u16(20), u16(22),
    f(24), f(28), f(32), f(36), f(40), f(44), f(48),
    f(52), f(56), f(60), f(64), f(68), f(72),
    f(76), f(80), f(84), f(88), f(92), f(96),
    f(152), f(156), f(160), f(164), f(168), f(172),
    u32(100), u32(104), u32(108), u32(112),
    f(116), f(120),
    u32(124), flags, ...FLAG_NAMES.map((_, b) => (flags >> b) & 1),
    v.getInt8(off + 130), u8(131), u8(132),
    u8(133), u8(134), u8(135),
    u32(136), u32(140), u16(144), u16(146),
    u8(148), u8(149), u8(150),
  ];
}
