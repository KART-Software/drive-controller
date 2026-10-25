"""Logs タブ (一覧 → CSV、開く → ズーム → 範囲 CSV) をヘッドレス Chrome で通し、CSV を tools/decode_log.py の出力と突き合わせる。
フォルダ / 保存先の picker は OPFS で代替する (本物のダイアログは自動化できない)。

準備 (1 回): uv run --with playwright python -m playwright install chromium
実行:
    cd dc-console && pnpm build && (pnpm preview --port 4174 --strictPort &)
    uv run --with playwright python scripts/e2e_logs.py http://localhost:4174 <dc_NNNN ディレクトリ>
"""
import csv
import subprocess
import sys
import tempfile
from pathlib import Path

from playwright.sync_api import sync_playwright

ROOT = Path(__file__).resolve().parents[2]
FAKE_PICKERS = """
window.showDirectoryPicker = async () => {
  const root = await navigator.storage.getDirectory();
  return await root.getDirectoryHandle('sd', { create: true });
};
window.showSaveFilePicker = async (o) => {
  const root = await navigator.storage.getDirectory();
  window.__savedName = o.suggestedName;
  return await root.getFileHandle(o.suggestedName, { create: true });
};
"""


def norm(cell):
    if cell == "":
        return ""
    try:
        return float(cell)
    except ValueError:
        return cell


def same_rows(a, b):
    if len(a) != len(b):
        return f"row count {len(a)} vs {len(b)}"
    for i, (ra, rb) in enumerate(zip(a, b)):
        for k in ra:
            if norm(ra[k]) != norm(rb[k]):
                return f"row {i} {k}: {ra[k]!r} vs {rb[k]!r}"
    return None


def main(base, run_dir):
    run_dir = Path(run_dir)
    tmp = Path(tempfile.mkdtemp(prefix="e2e_logs_"))
    py_csv = tmp / "py.csv"
    subprocess.run([sys.executable, str(ROOT / "tools" / "decode_log.py"), str(run_dir), str(py_csv)], check=True, capture_output=True)
    py_rows = list(csv.DictReader(open(py_csv, newline="")))
    errors = []
    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page(viewport={"width": 1300, "height": 900})
        page.on("console", lambda m: errors.append(m.text) if m.type == "error" else None)
        page.on("pageerror", lambda e: errors.append(str(e)))
        page.add_init_script(FAKE_PICKERS)
        page.route(f"{base}/__run/*", lambda route, req: route.fulfill(body=(run_dir / req.url.rsplit("/", 1)[-1]).read_bytes(), content_type="application/octet-stream"))
        page.goto(f"{base}/logs", wait_until="domcontentloaded")
        names = sorted(f.name for f in run_dir.glob("*.bin"))
        page.evaluate("""async ([dirName, names]) => {
          const root = await navigator.storage.getDirectory();
          const sd = await root.getDirectoryHandle('sd', { create: true });
          const run = await sd.getDirectoryHandle(dirName, { create: true });
          for (const n of names) {
            const buf = await (await fetch('/__run/' + n)).arrayBuffer();
            const fh = await run.getFileHandle(n, { create: true });
            const w = await fh.createWritable(); await w.write(buf); await w.close();
          }
        }""", [run_dir.name, names])

        def read_saved():
            name = page.evaluate("() => window.__savedName")
            text = page.evaluate("""async (name) => {
              const root = await navigator.storage.getDirectory();
              return await (await (await root.getFileHandle(name)).getFile()).text();
            }""", name)
            return name, list(csv.DictReader(text.splitlines()))

        # 一覧 → run 全体の CSV
        page.get_by_role("button", name="フォルダを開く").click()
        page.wait_for_selector("table.log-runs tbody tr", timeout=30000)
        row = page.locator("table.log-runs tbody tr").first
        print("run row:", row.locator("td").all_inner_texts()[:7])
        row.get_by_role("button", name="CSV").click()
        page.wait_for_selector("text=完了", timeout=180000)
        name, rows = read_saved()
        err = same_rows(rows, py_rows)
        print(f"full CSV ({name}): {len(rows)} rows,", err or "identical to decode_log.py")
        if err:
            errors.append("full csv: " + err)

        # 開く → ドラッグで拡大 → ホイールで生データまで → 範囲 CSV
        row.get_by_role("button", name="開く").click()
        page.wait_for_selector(".log-chart .uplot", timeout=180000)
        page.wait_for_timeout(1500)
        over = page.locator(".log-chart .u-over").first
        box = over.bounding_box()
        y = box["y"] + box["height"] / 2
        page.mouse.move(box["x"] + box["width"] * 0.30, y)
        page.mouse.down()
        page.mouse.move(box["x"] + box["width"] * 0.45, y, steps=8)
        page.mouse.up()
        page.wait_for_timeout(600)
        for _ in range(6):
            page.mouse.move(box["x"] + box["width"] / 2, y)
            page.mouse.wheel(0, -100)
            page.wait_for_timeout(250)
        print("panels:", page.locator(".log-chart .uplot").count(), "| range:", page.locator(".log-chart .log-toolbar span").first.inner_text())
        page.screenshot(path=str(tmp / "chart.png"), full_page=True)
        page.get_by_role("button", name="この範囲を CSV").click()
        # 直前の run 全体の「完了」が残っているので、範囲の名前 (run 名 + "_…s-…s") が付いた完了を待つ
        page.wait_for_selector(f".log-progress:has-text('{run_dir.name}_'):has-text('完了')", timeout=180000)
        name, rows = read_saved()
        t0, t1 = float(rows[0]["t_us"]), float(rows[-1]["t_us"])
        expected = [r for r in py_rows if t0 <= float(r["t_us"]) <= t1]
        err = same_rows(rows, expected)
        print(f"range CSV ({name}): {len(rows)} rows,", err or "identical to decode_log.py for that range")
        if err:
            errors.append("range csv: " + err)
        browser.close()
    print("screenshot:", tmp / "chart.png")
    print("console errors:", errors if errors else "none")
    return 1 if errors else 0


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1], sys.argv[2]))
