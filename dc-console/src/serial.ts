import type { Transport } from "./transport";

const BAUD_RATE = 115200;

let port: SerialPort | null = null;
let reader: ReadableStreamDefaultReader<Uint8Array> | null = null;
let writer: WritableStreamDefaultWriter<Uint8Array> | null = null;

let onBytes: ((chunk: Uint8Array) => void) | null = null;
let onDisconnect: (() => void) | null = null;

// 対象は Teensy (PJRC の VID 0x16C0) の drive-controller (PID 0x0483) と SD リーダー (0x04D5) だけ。
// ポート選択ダイアログにも CANable などの別の COM は出ない
const KART_PORTS = [
  { usbVendorId: 0x16c0, usbProductId: 0x0483 },
  { usbVendorId: 0x16c0, usbProductId: 0x04d5 },
];

function isKartPort(p: SerialPort): boolean {
  try {
    const info = p.getInfo();
    return KART_PORTS.some((f) => f.usbVendorId === info.usbVendorId && f.usbProductId === info.usbProductId);
  } catch {
    return false;
  }
}

// 許可済み (過去にダイアログで選んだ。Chrome がデバイス単位で覚える) の KART デバイスがちょうど 1 台つながっていれば
// ダイアログなしでそれに接続。0 台か 2 台以上ならダイアログで選ぶ (許可していないデバイスはページから見えないので数えられない)。
// chooser=true なら必ずダイアログ (Connect ▾ の「ポートを選び直して接続」)。auto=true はページを開いたとき / USB を挿したときの
// 自動接続用で、ダイアログを出せない (ユーザー操作が無い) ので許可済みが 1 台でなければ何もせず false
async function connect(options?: { chooser?: boolean; auto?: boolean }): Promise<boolean> {
  let chosen: SerialPort | null = null;
  if (!options?.chooser) {
    const granted = (await navigator.serial.getPorts()).filter(isKartPort);
    if (granted.length === 1) chosen = granted[0];
  }
  if (!chosen) {
    if (options?.auto) return false;
    chosen = await navigator.serial.requestPort({ filters: KART_PORTS });
  }
  port = chosen;
  await port.open({ baudRate: BAUD_RATE });
  // DTR を明示的に立てる (Web Serial の既定挙動に依存しない)。ファームは起動時に Serial (=DTR) を最大 3 s
  // 待つ。RTS は使わないので触らない (RTS でリセットがかかる機器があるため)。閉じる側は OS が DTR を落とす。
  try {
    await port.setSignals({ dataTerminalReady: true });
  } catch {
    // 非対応環境では既定挙動 (open 時に DTR が立つ) に任せる
  }

  writer = port.writable!.getWriter();
  reader = (port.readable as ReadableStream<Uint8Array>).getReader();

  readBytes();

  port.addEventListener("disconnect", () => {
    cleanup();
    onDisconnect?.();
  });
  return true;
}

// 許可済みのデバイスが挿された (再列挙された) とき。SD リーダーや Reboot の後の自動再接続に使う
function setOnDeviceAttached(fn: () => void): void {
  try {
    navigator.serial.addEventListener("connect", () => fn());
  } catch {
    // Web Serial 非対応
  }
}

async function readBytes(): Promise<void> {
  try {
    while (true) {
      const { value, done } = await reader!.read();
      if (done) break;
      if (value && value.length > 0) onBytes?.(value);
    }
  } catch {
    // Port closed or error
  }
}

async function send(bytes: Uint8Array): Promise<void> {
  if (!writer) return;
  await writer.write(bytes);
}

async function disconnect(): Promise<void> {
  await cleanup();
}

async function cleanup(): Promise<void> {
  const r = reader;
  const w = writer;
  const p = port;
  reader = null;
  writer = null;
  port = null;

  try {
    await r?.cancel();
  } catch {}
  try {
    r?.releaseLock();
  } catch {}
  try {
    await w?.close();
  } catch {}
  try {
    w?.releaseLock();
  } catch {}
  try {
    await p?.close();
  } catch {}
}

function deviceInfo(): { usbVendorId?: number; usbProductId?: number } | null {
  try {
    return port ? port.getInfo() : null;
  } catch {
    return null;
  }
}

function isConnected(): boolean {
  return port !== null;
}

export const serial: Transport = {
  connect,
  disconnect,
  send,
  isConnected,
  setOnBytes(fn) {
    onBytes = fn;
  },
  setOnDisconnect(fn) {
    onDisconnect = fn;
  },
  deviceInfo,
  setOnDeviceAttached,
};
