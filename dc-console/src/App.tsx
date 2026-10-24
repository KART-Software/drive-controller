import { useState, useCallback, useRef, useEffect } from "preact/hooks";
import { serial, SD_READER_PORT } from "./serial";
import { mockSerial } from "./mock-serial";
import { protocol } from "./protocol";
import type { Transport } from "./transport";
import type { DeviceConfig } from "./types";
import { type LogEntry } from "./components/DebugLog";
import { EtcView } from "./components/EtcView";
import { DrivetrainView } from "./components/DrivetrainView";
import { sensorStore } from "./sensor-store";
import { drivetrainStore } from "./drivetrain-store";
import { ConfirmDialog } from "./components/ConfirmDialog";

const MAX_LOG_ENTRIES = 100;
const REORDER_WINDOW_US = 1_000_000;
const isMock = new URLSearchParams(window.location.search).has("mock");

type Route = "etc" | "drivetrain";

// Vite の base ("/" or "/etc/dev/" 等)。末尾は必ず "/"。
const BASE = import.meta.env.BASE_URL;

function routeFromPath(): Route {
  let p = window.location.pathname;
  if (p.startsWith(BASE)) p = p.slice(BASE.length);
  p = p.replace(/^\/+|\/+$/g, "");
  return p === "drivetrain" ? "drivetrain" : "etc";
}

function pathForRoute(r: Route): string {
  // クエリ (?mock 等) は保持する
  return BASE + (r === "drivetrain" ? "drivetrain" : "") + window.location.search;
}

export function App() {
  const transportRef = useRef<Transport>(isMock ? mockSerial : serial);
  const [connected, setConnected] = useState(false);
  // つないだ相手: drive-controller (0x16C0 / 0x0483) か SD リーダー (別ファーム、0x1209 / 0x0001)。SD リーダーのときは通常モードへ戻すボタンだけ出す
  const [sdReader, setSdReader] = useState(false);
  const [config, setConfig] = useState<DeviceConfig | null>(null);
  const [dirty, setDirty] = useState(false);
  const [logs, setLogs] = useState<LogEntry[]>([]);
  const [route, setRoute] = useState<Route>(routeFromPath());
  const [fs, setFs] = useState<{ used: number; total: number } | null>(null);
  const [fsDialogOpen, setFsDialogOpen] = useState(false);
  const [confirm, setConfirm] = useState<null | "format_sd" | "sd_reader">(null);  // 開いている確認ダイアログ
  const [connectMenuOpen, setConnectMenuOpen] = useState(false);  // Connect の右端 ▾ のメニュー
  const autoSuppressed = useRef(false);  // Disconnect を押した後は、USB を挿し直すまで自動接続しない
  const fsDismissed = useRef(false);

  useEffect(() => {
    const onPop = () => setRoute(routeFromPath());
    window.addEventListener("popstate", onPop);
    return () => window.removeEventListener("popstate", onPop);
  }, []);

  function navTo(e: MouseEvent, r: Route) {
    // 修飾クリック (Ctrl/⌘/Shift/中クリック) は「新規タブで開く」等に任せる。
    // 通常クリックのみ pushState で SPA 内遷移 → 接続を維持したままページ切替。
    if (e.metaKey || e.ctrlKey || e.shiftKey || e.altKey || e.button !== 0) return;
    e.preventDefault();
    window.history.pushState(null, "", pathForRoute(r));
    setRoute(r);
  }

  // deviceUs = デバイスの debug メッセージの時刻 (DebugLogger::log が呼ばれた時刻、起動からの µs)。
  // ISR からのメッセージは後からまとめて届くので、届いた順と呼ばれた順が前後しうる。直前のデバイスの
  // メッセージより古ければ、時刻の順になる位置へ差し込む。console 自身のメッセージ (deviceUs なし) は越えない。
  // REORDER_WINDOW_US より離れたものとは入れ替えない (デバイスの再起動で時刻が 0 に戻った場合など)
  const addLog = useCallback((msg: string, deviceUs?: number) => {
    const ts = deviceUs != null ? (deviceUs / 1e6).toFixed(3) + "s" : new Date().toLocaleTimeString();
    const entry: LogEntry = { ts, msg, deviceUs };
    setLogs((prev) => {
      let i = prev.length;
      if (deviceUs != null) {
        while (i > 0) {
          const p = prev[i - 1].deviceUs;
          if (p == null || p <= deviceUs || p - deviceUs > REORDER_WINDOW_US) break;
          i--;
        }
      }
      const next = [...prev.slice(0, i), entry, ...prev.slice(i)];
      return next.length > MAX_LOG_ENTRIES ? next.slice(-MAX_LOG_ENTRIES) : next;
    });
  }, []);

  // ConfigResponse (get_config / save / revert / format_fs) を一括反映。
  // FS 使用量も取り込み、90% 以上なら (未 dismiss なら) フォーマットダイアログを開く。
  function applyConfigResp(data: Record<string, unknown>) {
    setConfig(data as unknown as DeviceConfig);
    setDirty(!!data.configChanged);
    const used = data.fsUsed as number | undefined;
    const total = data.fsTotal as number | undefined;
    if (typeof used === "number" && typeof total === "number" && total > 0) {
      setFs({ used, total });
      if (used >= total * 0.9 && !fsDismissed.current) setFsDialogOpen(true);
    }
  }

  function handleFormatFs() {
    addLog("Formatting FS (keeping config)...");
    protocol.sendCommand("format_fs")
      .then((resp) => {
        if (resp.ok && resp.data) {
          applyConfigResp(resp.data as Record<string, unknown>);
          addLog("FS formatted, config preserved");
        } else {
          addLog("FS format failed");
        }
        setFsDialogOpen(false);
        fsDismissed.current = false;
      })
      .catch((err: Error) => addLog("Format error: " + err.message));
  }

  // SD カードを exFAT でフォーマット (中身は消える)。ファーム側は ETC 動作中なら拒否する
  function handleFormatSd() {
    setConfirm(null);
    addLog("Formatting SD card...");
    protocol.sendCommand("format_sd")
      .then((resp) => addLog(resp.ok ? "SD card formatted (exFAT), logging restarted" : "SD format refused or failed (ETC on?)"))
      .catch((err: Error) => addLog("SD format error: " + err.message));
  }

  // SD リーダー (別ファーム dc-sd-reader) で再起動: SD カードが USB ドライブ "KART SD Reader" (Mass Storage) として出る。ETC 動作中なら拒否。
  // 応答のあと装置がリセットされるので切断になる。USB を抜くか Reboot で drive-controller に戻る
  function handleSdReader() {
    setConfirm(null);
    addLog("Rebooting into SD reader mode...");
    protocol.sendCommand("reboot", { sdReader: true })
      .then((resp) => addLog(resp.ok ? "SD card will appear as USB drive 'KART SD Reader'. Eject it to return" : "SD reader refused (ETC on?)"))
      .catch((err: Error) => addLog("SD reader error: " + err.message));
  }

  function handleRtcSync() {
    const epoch = Math.floor(Date.now() / 1000);
    protocol.sendCommand("set_rtc", { epoch })
      .then(() => addLog(`RTC synced: ${new Date().toLocaleString()}`))
      .catch((err: Error) => addLog("RTC sync error: " + err.message));
  }

  // Wire protocol callbacks once
  useEffect(() => {
    sensorStore.open().then(() => sensorStore.cleanOldSessions());
    const t = transportRef.current;
    protocol.setTransport(t);

    protocol.setOnSensorData((data) => {
      sensorStore.push(data);
      drivetrainStore.push(data);
    });
    protocol.setOnDebugLog((msg, deviceUs) => addLog(msg, deviceUs));
    if (import.meta.env.DEV) {
      protocol.setOnSerialLog((dir, line) => {
        console.log(`[Serial ${dir.toUpperCase()}]`, line);
      });
    }
    t.setOnDisconnect(() => {
      setConnected(false);
      addLog("Disconnected");
    });
    if (isMock) addLog("Mock mode enabled");
    // 自動接続: ページを開いたとき、許可済みの KART デバイスが 1 台つながっていればつなぐ。
    // USB を挿したとき (SD リーダーや Reboot で再列挙されたときも) も同じ
    t.setOnDeviceAttached(() => {
      autoSuppressed.current = false;
      void handleConnect(false, true);
    });
    void handleConnect(false, true);
  }, [addLog]);

  const webSerialAvailable = isMock || "serial" in navigator;

  // chooser: ポート選択ダイアログを必ず出す (Connect ▾ の「ポートを選び直して接続」)。auto: ページを開いたとき / USB を挿したときの
  // 自動接続 (許可済みが 1 台のときだけ。ダイアログは出さない)。Disconnect を押した後は自動接続しない (Connect を押せば戻る)
  async function handleConnect(chooser = false, auto = false) {
    setConnectMenuOpen(false);
    if (auto && (autoSuppressed.current || transportRef.current.isConnected())) return;
    if (!auto) autoSuppressed.current = false;
    try {
      if (!(await transportRef.current.connect({ chooser, auto }))) return;
      const info = transportRef.current.deviceInfo();
      const isSdReader = info?.usbVendorId === SD_READER_PORT.usbVendorId && info?.usbProductId === SD_READER_PORT.usbProductId;
      setSdReader(isSdReader);
      if (!isSdReader) {  // SD リーダーには telemetry が無いのでセッションを作らない
        sensorStore.startSession();
        drivetrainStore.reset();
      }
      setConnected(true);
      fsDismissed.current = false;  // 新規接続ごとに FS フル警告を再評価
      addLog((isSdReader ? "Connected: SD card is mounted on the PC as USB drive 'KART SD Reader'" : "Connected") + (auto ? " (auto)" : ""));
      if (isSdReader) return;
      setTimeout(async () => {
        try {
          const resp = await protocol.sendCommand("get_config");
          if (resp.ok && resp.data) {
            applyConfigResp(resp.data as Record<string, unknown>);
            addLog("Config loaded from device");
          }
        } catch {}
      }, 500);
    } catch (err) {
      addLog("Connection failed: " + (err as Error).message);
    }
  }

  async function handleDisconnect() {
    autoSuppressed.current = true;  // 自分で切ったら、挿し直すまで自動接続しない
    await sensorStore.flush();
    await transportRef.current.disconnect();
    setConnected(false);
    setSdReader(false);
    addLog("Disconnected");
  }

  // SD リーダー → drive-controller に戻す (RebootCmd NORMAL)。応答のあと装置がリセットされるので切断になる
  function handleRebootToNormal() {
    addLog("Rebooting to normal mode...");
    protocol.sendCommand("reboot")
      .then(() => addLog("Rebooting. Reconnect in a few seconds"))
      .catch((err: Error) => addLog("Reboot error: " + err.message));
  }

  function handleSave() {
    protocol.sendCommand("save")
      .then((resp) => {
        if (resp.ok && resp.data) applyConfigResp(resp.data as Record<string, unknown>);
        addLog("Config saved");
      })
      .catch((err: Error) => addLog("Save error: " + err.message));
  }

  function handleRevert() {
    protocol.sendCommand("revert")
      .then((resp) => {
        if (resp.ok && resp.data) applyConfigResp(resp.data as Record<string, unknown>);
        addLog("Config reverted");
      })
      .catch((err: Error) => addLog("Revert error: " + err.message));
  }

  return (
    <div id="app-root">
      <header>
        <h1>Drive Controller</h1>
        <nav class="page-nav">
          <a class={`nav-tab${route === "etc" ? " active" : ""}`} href={pathForRoute("etc")} onClick={(e) => navTo(e, "etc")}>ETC</a>
          <a class={`nav-tab${route === "drivetrain" ? " active" : ""}`} href={pathForRoute("drivetrain")} onClick={(e) => navTo(e, "drivetrain")}>Drivetrain</a>
        </nav>
        {!sdReader && <div class="header-actions">
          <button class="danger" disabled={!connected} onClick={() => protocol.sendCommand("motor_off").catch((err: Error) => addLog("Command error: " + err.message))}>Motor OFF</button>
          <button class="danger" disabled={!connected} onClick={() => protocol.sendCommand("reboot").catch((err: Error) => addLog("Command error: " + err.message))}>Reboot</button>
          <button disabled={!connected} onClick={handleRtcSync} title="デバイスのRTCをこのPCの現在時刻に同期">RTC Sync</button>
          <button class="danger" disabled={!connected} onClick={() => setConfirm("format_sd")} title="SD カードを exFAT でフォーマット (ログは消える)。ETC 停止中のみ">Format SD</button>
          <button disabled={!connected} onClick={() => setConfirm("sd_reader")} title="SD カードを PC にマウント (別ファームで再起動)。ETC 停止中のみ">SD Reader</button>
          <button disabled={sensorStore.totalRows === 0} onClick={async () => {
            try {
              const blob = await sensorStore.exportCsv();
              const url = URL.createObjectURL(blob);
              const a = document.createElement("a");
              a.href = url;
              a.download = `sensor-${new Date().toISOString().slice(0, 19).replace(/:/g, "")}.csv`;
              a.click();
              URL.revokeObjectURL(url);
              addLog(`CSV exported (${sensorStore.totalRows} rows)`);
            } catch (err) { addLog("CSV export error: " + (err as Error).message); }
          }}>Export CSV</button>
        </div>}
        <div class="connection-controls">
          <div class="split-button">
            <button onClick={() => handleConnect(false)} disabled={connected || !webSerialAvailable} title="許可済みの KART デバイスが 1 台つながっていれば自動で接続、それ以外はポートを選ぶ">Connect</button>
            <button class="split-button-toggle" onClick={() => setConnectMenuOpen(!connectMenuOpen)} disabled={connected || !webSerialAvailable} title="接続のオプション">▾</button>
            {connectMenuOpen && (
              <>
                <div class="menu-backdrop" onClick={() => setConnectMenuOpen(false)} />
                <div class="split-button-menu">
                  <button onClick={() => handleConnect(true)}>ポートを選び直して接続</button>
                </div>
              </>
            )}
          </div>
          <button onClick={handleDisconnect} disabled={!connected}>Disconnect</button>
          <span class={`status-badge ${connected ? "connected" : "disconnected"}`}>
            {connected ? "Connected" : "Disconnected"}
          </span>
        </div>
      </header>
      {dirty && (
        <div class="action-bar">
          <span class="action-bar-label">Unsaved changes</span>
          <button onClick={handleSave}>Save</button>
          <button onClick={handleRevert}>Revert</button>
        </div>
      )}

      {sdReader ? (
        <section style={{ margin: "24px", maxWidth: "520px" }}>
          <h2>SD カードを PC にマウント中</h2>
          <p style={{ color: "var(--text-dim)" }}>
            USB ドライブ「KART SD Reader」として読み書きできます。制御は止まっています。PC で取り出し (Eject) をすると通常モードに戻ります。
          </p>
          <button class="danger" onClick={handleRebootToNormal}>通常モードで再起動</button>
        </section>
      ) : route === "drivetrain" ? (
        <DrivetrainView config={config} setConfig={setConfig} addLog={addLog} setDirty={setDirty} logs={logs} />
      ) : (
        <EtcView config={config} setConfig={setConfig} addLog={addLog} setDirty={setDirty} logs={logs} />
      )}

      {!webSerialAvailable && (
        <section style={{ margin: "10px" }}>
          <p style={{ color: "var(--err)" }}>Web Serial API is not available. Use Chrome or Edge.</p>
        </section>
      )}

      <ConfirmDialog open={fsDialogOpen} title="ストレージ (FS) がいっぱいです" confirmLabel="設定を保持してフォーマット" danger
        confirmDisabled={!connected}
        onConfirm={handleFormatFs}
        onCancel={() => { setFsDialogOpen(false); fsDismissed.current = true; }}>
        <p>使用量: {fs ? `${fs.used} / ${fs.total} bytes (${Math.round((fs.used / fs.total) * 100)}%)` : "-"}</p>
        <p>現在の設定を保持したまま FS をフォーマットして空き容量を回復します。設定は自動で書き戻されます (フォーマット中は一瞬書き込みが止まります)。</p>
      </ConfirmDialog>
      <ConfirmDialog open={confirm === "format_sd"} title="SD カードをフォーマット" confirmLabel="フォーマット" danger
        onConfirm={handleFormatSd} onCancel={() => setConfirm(null)}>
        <p>カードの中身 (ログ) はすべて消えます。</p>
      </ConfirmDialog>
      <ConfirmDialog open={confirm === "sd_reader"} title="SD カードを PC にマウント" confirmLabel="マウント"
        onConfirm={handleSdReader} onCancel={() => setConfirm(null)}>
        <p>別のファームで再起動し、制御は止まります。</p>
        <p>戻すときは PC で取り出し (Eject) をするか、「通常モードで再起動」を押すか、USB を抜いてください。</p>
      </ConfirmDialog>
    </div>
  );
}
