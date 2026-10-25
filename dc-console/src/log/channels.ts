// チャート用のチャネル表: レコード内の位置と型、パネルへの割り当て。レイアウトは log_record.ts (= log_record.hpp) に従う。
// CSV の列 (recordToRow) とは独立。
export type ChannelType = "f32" | "u32" | "u16" | "u8" | "i8" | "bit";

export interface Channel {
  key: string;
  offset: number;
  type: ChannelType;
  bit?: number; // type "bit": offset の u16 の何ビット目か
}

export interface Panel {
  id: string;
  title: string;
  unit: string;
  channels: string[];
  stepped?: boolean; // 段状に描く (状態値)
}

const f32 = (key: string, offset: number): Channel => ({ key, offset, type: "f32" });

export const CHANNELS: Channel[] = [
  ...Array.from({ length: 8 }, (_, i): Channel => ({ key: `adc${i}`, offset: 8 + 2 * i, type: "u16" })),
  f32("apps1", 24), f32("apps2", 28), f32("ittr", 32), f32("tps1", 36), f32("tps2", 40), f32("bps", 44), f32("clutch", 48),
  f32("ax", 52), f32("ay", 56), f32("az", 60), f32("gx", 64), f32("gy", 68), f32("gz", 72),
  f32("wheel_fl_hz", 76), f32("wheel_fr_hz", 80), f32("wheel_rl_hz", 84), f32("wheel_rr_hz", 88), f32("engine_hz", 92), f32("clutch_hz", 96),
  { key: "wc_fl", offset: 100, type: "u32" }, { key: "wc_fr", offset: 104, type: "u32" }, { key: "wc_rl", offset: 108, type: "u32" }, { key: "wc_rr", offset: 112, type: "u32" },
  f32("target_tp", 116), f32("duty", 120),
  { key: "errors", offset: 124, type: "u32" }, { key: "flags", offset: 128, type: "u16" },
  ...["f_manual", "f_ittr", "f_shutdown", "f_valid", "f_autoshift", "f_launch", "f_starter_relay", "f_starter_switch"].map(
    (key, bit): Channel => ({ key, offset: 128, type: "bit", bit }),
  ),
  { key: "gear", offset: 130, type: "i8" }, { key: "mode", offset: 131, type: "u8" }, { key: "autoshift_state", offset: 132, type: "u8" },
  { key: "exp_type", offset: 133, type: "u8" }, { key: "exp_index", offset: 134, type: "u8" }, { key: "exp_phase", offset: 135, type: "u8" },
  { key: "loop_max_us", offset: 136, type: "u32" }, { key: "sd_max_us", offset: 140, type: "u32" },
  { key: "tick_max_us", offset: 144, type: "u16" }, { key: "log_drops", offset: 146, type: "u16" },
  { key: "can_fault", offset: 148, type: "u8" }, { key: "can_tec", offset: 149, type: "u8" }, { key: "can_rec", offset: 150, type: "u8" },
  f32("wheel_fl_kmh", 152), f32("wheel_fr_kmh", 156), f32("wheel_rl_kmh", 160), f32("wheel_rr_kmh", 164), f32("engine_rpm", 168), f32("clutch_rpm", 172),
];

export const CHANNEL_INDEX: Record<string, number> = Object.fromEntries(CHANNELS.map((c, i) => [c.key, i]));

export const PANELS: Panel[] = [
  { id: "throttle", title: "スロットル", unit: "%", channels: ["apps1", "apps2", "ittr", "tps1", "tps2", "target_tp", "duty"] },
  { id: "speed", title: "車速", unit: "km/h", channels: ["wheel_fl_kmh", "wheel_fr_kmh", "wheel_rl_kmh", "wheel_rr_kmh"] },
  { id: "rpm", title: "回転", unit: "rpm", channels: ["engine_rpm", "clutch_rpm"] },
  { id: "brake", title: "ブレーキ / クラッチ", unit: "", channels: ["bps", "clutch"] },
  { id: "accel", title: "加速度", unit: "mg", channels: ["ax", "ay", "az"] },
  { id: "gyro", title: "角速度", unit: "dps", channels: ["gx", "gy", "gz"] },
  {
    id: "status", title: "状態", unit: "", stepped: true,
    channels: ["gear", "mode", "autoshift_state", "f_valid", "f_shutdown", "f_manual", "f_ittr", "f_autoshift", "f_launch", "f_starter_relay", "f_starter_switch"],
  },
  { id: "system", title: "システム", unit: "µs", stepped: true, channels: ["loop_max_us", "sd_max_us", "tick_max_us", "log_drops", "can_fault", "can_tec", "can_rec"] },
  { id: "adc", title: "ADC 生値", unit: "raw", channels: ["adc0", "adc1", "adc2", "adc3", "adc4", "adc5", "adc6", "adc7"] },
];

export function readChannel(v: DataView, off: number, ch: Channel): number {
  switch (ch.type) {
    case "f32": return v.getFloat32(off + ch.offset, true);
    case "u32": return v.getUint32(off + ch.offset, true);
    case "u16": return v.getUint16(off + ch.offset, true);
    case "u8": return v.getUint8(off + ch.offset);
    case "i8": return v.getInt8(off + ch.offset);
    case "bit": return (v.getUint16(off + ch.offset, true) >> (ch.bit ?? 0)) & 1;
  }
}
