// チャート用のデータ (src/log/overview.ts) の検証: 概観の min / max と範囲クエリを、recordToRow (CSV と同じ経路、
// decode_log.py と一致済み) から総当たりで出した値と比べる。
//   pnpm dlx tsx scripts/check_chart.mts <dc_NNNN ディレクトリ>
import { openAsBlob } from "node:fs";
import { readdir } from "node:fs/promises";
import { join } from "node:path";
import { CHANNEL_INDEX } from "../src/log/channels";
import { buildOverview, queryRange } from "../src/log/overview";
import { COLUMNS, HEADER_SIZE, RECORD_SIZE, parseHeader, recordToRow } from "../src/log/log_record";

const dir = process.argv[2];
if (!dir) { console.error("usage: check_chart.mts <dc_NNNN dir>"); process.exit(2); }
const names = (await readdir(dir)).filter((n) => /^\d{4}\.bin$/.test(n)).sort();
const files = await Promise.all(names.map((n) => openAsBlob(join(dir, n))));

const t0 = Date.now();
const ov = await buildOverview(files);
console.log(`overview: ${ov.n} buckets × ${ov.bucketUs} µs, ${ov.gaps.length} gaps, ${ov.files.length} files (${((Date.now() - t0) / 1000).toFixed(2)} s)`);

// 総当たり: 全レコードを recordToRow で読み、チャネルごとにバケットの min / max を出す
const check = ["apps1", "engine_rpm", "gear", "f_valid", "loop_max_us", "adc3", "wheel_fl_kmh", "flags"];
const col = (k: string) => COLUMNS.indexOf(k);
const bmin = check.map(() => new Float64Array(ov.n).fill(Infinity));
const bmax = check.map(() => new Float64Array(ov.n).fill(-Infinity));
const rows: { t: number; v: number[] }[] = [];
for (const file of files) {
  const h = parseHeader(await file.slice(0, HEADER_SIZE).arrayBuffer(), "x");
  if (!h) continue;
  const n = Math.floor((file.size - HEADER_SIZE) / RECORD_SIZE);
  const v = new DataView(await file.slice(HEADER_SIZE, HEADER_SIZE + n * RECORD_SIZE).arrayBuffer());
  for (let i = 0; i < n; i++) {
    const row = recordToRow(v, i * RECORD_SIZE, h);
    const t = row[0] as number;
    if (t === 0 || t > ov.t1Us) break;
    const vals = check.map((k) => row[col(k)] as number);
    rows.push({ t, v: vals });
    const b = Math.min(ov.n - 1, Math.floor((t - ov.t0Us) / ov.bucketUs));
    vals.forEach((x, k) => { if (x < bmin[k][b]) bmin[k][b] = x; if (x > bmax[k][b]) bmax[k][b] = x; });
  }
}
let bad = 0;
check.forEach((k, ci) => {
  const c = CHANNEL_INDEX[k];
  for (let b = 0; b < ov.n; b++) {
    const okMin = (bmin[ci][b] === Infinity && ov.min[c][b] === Infinity) || Math.fround(bmin[ci][b]) === ov.min[c][b];
    const okMax = (bmax[ci][b] === -Infinity && ov.max[c][b] === -Infinity) || Math.fround(bmax[ci][b]) === ov.max[c][b];
    if (!okMin || !okMax) { if (bad++ < 5) console.log(`MISMATCH ${k} bucket ${b}: brute ${bmin[ci][b]}..${bmax[ci][b]} overview ${ov.min[c][b]}..${ov.max[c][b]}`); }
  }
});
console.log(bad ? `overview: ${bad} mismatches` : `overview: ${check.length} channels × ${ov.n} buckets match brute force`);

// 範囲クエリ (生): 2 s の窓を幅 4000 px で → 生の値がレコードと一致するか
const q0 = ov.t0Us + 5_000_000, q1 = q0 + 2_000_000;
const chans = check.map((k) => CHANNEL_INDEX[k]);
const raw = await queryRange(files, ov, q0, q1, 4000, chans);
const res0 = (q1 - q0) / 4000;
const expRaw = rows.filter((r) => r.t >= q0 - res0 && r.t <= q1 + res0);
let rawBad = !raw.raw || raw.t.length < expRaw.length || raw.t.length > expRaw.length + 1; // hi を超える最初の 1 つは含んでよい
if (!rawBad) for (let i = 0; i < expRaw.length; i++) { if (raw.t[i] !== expRaw[i].t) { rawBad = true; break; } for (let k = 0; k < chans.length; k++) if (raw.min[k][i] !== Math.fround(expRaw[i].v[k])) { rawBad = true; break; } }
console.log(rawBad ? `raw query: MISMATCH (raw=${raw.raw}, ${raw.t.length} vs ${expRaw.length} points)` : `raw query: ${raw.t.length} points match records`);

// 範囲クエリ (画素バケット): 画素あたりの時間が概観のバケットより細かく、かつレコードが画素 × 2 より多い窓
// (ここでは幅 1000 px に 2.05 s) → ファイルからの再バケットが総当たりと一致するか
const width = 1000;
const p0 = ov.t0Us + 3_000_000, p1 = p0 + Math.round(width * ov.bucketUs * 0.97);
const px = await queryRange(files, ov, p0, p1, width, chans);
const res = (p1 - p0) / width, lo = p0 - res, hi = p1 + res;
let pxBad = px.raw;
if (!pxBad) {
  const nb = Math.ceil((hi - lo) / res);
  if (px.t.length !== nb) { pxBad = true; console.log(`pixel query: ${px.t.length} buckets, expected ${nb}`); }
  const mn = chans.map(() => new Float64Array(nb).fill(Infinity)), mx = chans.map(() => new Float64Array(nb).fill(-Infinity));
  // クエリは lo <= t < hi と、hi 以上の最初の 1 レコードを読む (lowerBound + 1)。同じ集合で総当たりする
  const firstAtOrAfter = rows.find((r) => r.t >= hi);
  for (const r of rows) {
    if (r.t < lo || (r.t >= hi && r !== firstAtOrAfter)) continue;
    const b = Math.min(nb - 1, Math.floor((r.t - lo) / res));
    r.v.forEach((x, k) => { if (x < mn[k][b]) mn[k][b] = x; if (x > mx[k][b]) mx[k][b] = x; });
  }
  for (let k = 0; k < chans.length && !pxBad; k++) for (let b = 0; b < nb; b++) {
    const emin = mn[k][b] === Infinity ? NaN : Math.fround(mn[k][b]), emax = mx[k][b] === -Infinity ? NaN : Math.fround(mx[k][b]);
    if (!Object.is(emin, px.min[k][b]) || !Object.is(emax, px.max[k][b])) { pxBad = true; console.log(`MISMATCH pixel ${check[k]} b${b}: brute ${emin}..${emax} query ${px.min[k][b]}..${px.max[k][b]}`); break; }
  }
}
console.log(pxBad ? "pixel query: MISMATCH" : `pixel query: ${px.t.length} buckets match brute force`);
process.exit(bad || rawBad || pxBad ? 1 : 0);
