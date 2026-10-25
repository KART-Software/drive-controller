// console のログデコーダ (src/log/csv-encode.ts) を Node で走らせて CSV にする検証用。tools/decode_log.py の出力と
// tools/compare_csv.py で比べる。
//   pnpm dlx tsx scripts/export_csv.mts <dc_NNNN ディレクトリ> <out.csv>
import { openAsBlob } from "node:fs";
import { readdir, open } from "node:fs/promises";
import { join } from "node:path";
import { encodeRun } from "../src/log/csv-encode";

const [dir, outPath] = process.argv.slice(2);
if (!dir || !outPath) {
  console.error("usage: export_csv.mts <dc_NNNN dir> <out.csv>");
  process.exit(2);
}
const names = (await readdir(dir)).filter((n) => /^\d{4}\.bin$/.test(n)).sort();
const files = await Promise.all(names.map((n) => openAsBlob(join(dir, n))));
const out = await open(outPath, "w");
const t0 = Date.now();
const r = await encodeRun(files, names, 1, {
  rows: async (text) => { await out.write(text); },
});
await out.close();
console.error(`${r.records} records, ${r.gaps} gaps${r.skipped.length ? `, skipped ${r.skipped.join(" ")}` : ""} -> ${outPath} (${((Date.now() - t0) / 1000).toFixed(1)} s)`);
