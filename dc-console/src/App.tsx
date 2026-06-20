import { useState, useCallback, useRef, useEffect } from "preact/hooks";
import { serial } from "./serial";
import { mockSerial } from "./mock-serial";
import { protocol } from "./protocol";
import type { Transport } from "./transport";
import type { DeviceConfig } from "./types";
import { type LogEntry } from "./components/DebugLog";
import { EtcView } from "./components/EtcView";
import { DrivetrainView } from "./components/DrivetrainView";
import { sensorStore } from "./sensor-store";
import { drivetrainStore } from "./drivetrain-store";

const MAX_LOG_ENTRIES = 100;
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
  const [config, setConfig] = useState<DeviceConfig | null>(null);
  const [dirty, setDirty] = useState(false);
  const [logs, setLogs] = useState<LogEntry[]>([]);
  const [route, setRoute] = useState<Route>(routeFromPath());

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

  const addLog = useCallback((msg: string, ts?: number) => {
    const tsStr = ts != null ? (ts / 1000).toFixed(1) + "s" : new Date().toLocaleTimeString();
    setLogs((prev) => {
      const next = [...prev, { ts: tsStr, msg }];
      return next.length > MAX_LOG_ENTRIES ? next.slice(-MAX_LOG_ENTRIES) : next;
    });
  }, []);

  // Wire protocol callbacks once
  useEffect(() => {
    sensorStore.open().then(() => sensorStore.cleanOldSessions());
    const t = transportRef.current;
    protocol.setTransport(t);

    protocol.setOnSensorData((data) => {
      sensorStore.push(data);
      drivetrainStore.push(data);
    });
    protocol.setOnDebugLog((msg, ts) => addLog(msg, ts));
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
  }, [addLog]);

  const webSerialAvailable = isMock || "serial" in navigator;

  async function handleConnect() {
    try {
      await transportRef.current.connect();
      sensorStore.startSession();
      drivetrainStore.reset();
      setConnected(true);
      addLog("Connected");
      setTimeout(async () => {
        try {
          const resp = await protocol.sendCommand("get_config");
          if (resp.ok && resp.data) {
            setConfig(resp.data as unknown as DeviceConfig);
            setDirty(!!(resp.data as Record<string, unknown>).configChanged);
            addLog("Config loaded from device");
          }
        } catch {}
      }, 500);
    } catch (err) {
      addLog("Connection failed: " + (err as Error).message);
    }
  }

  async function handleDisconnect() {
    await sensorStore.flush();
    await transportRef.current.disconnect();
    setConnected(false);
    addLog("Disconnected");
  }

  function handleSave() {
    protocol.sendCommand("save")
      .then((resp) => {
        if (resp.ok && resp.data) {
          setConfig(resp.data as unknown as DeviceConfig);
          setDirty(!!(resp.data as Record<string, unknown>).configChanged);
        }
        addLog("Config saved");
      })
      .catch((err: Error) => addLog("Save error: " + err.message));
  }

  function handleRevert() {
    protocol.sendCommand("revert")
      .then((resp) => {
        if (resp.ok && resp.data) {
          setConfig(resp.data as unknown as DeviceConfig);
          setDirty(!!(resp.data as Record<string, unknown>).configChanged);
        }
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
        <div class="header-actions">
          <button class="danger" disabled={!connected} onClick={() => protocol.sendCommand("motor_off").catch((err: Error) => addLog("Command error: " + err.message))}>Motor OFF</button>
          <button class="danger" disabled={!connected} onClick={() => protocol.sendCommand("reboot").catch((err: Error) => addLog("Command error: " + err.message))}>Reboot</button>
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
        </div>
        <div class="connection-controls">
          <button onClick={handleConnect} disabled={connected || !webSerialAvailable}>Connect</button>
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

      {route === "drivetrain" ? (
        <DrivetrainView config={config} setConfig={setConfig} addLog={addLog} setDirty={setDirty} logs={logs} />
      ) : (
        <EtcView config={config} setConfig={setConfig} addLog={addLog} setDirty={setDirty} logs={logs} />
      )}

      {!webSerialAvailable && (
        <section style={{ margin: "10px" }}>
          <p style={{ color: "var(--err)" }}>Web Serial API is not available. Use Chrome or Edge.</p>
        </section>
      )}
    </div>
  );
}
