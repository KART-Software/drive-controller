import type { Transport } from "./transport";

const BAUD_RATE = 115200;

let port: SerialPort | null = null;
let reader: ReadableStreamDefaultReader<Uint8Array> | null = null;
let writer: WritableStreamDefaultWriter<Uint8Array> | null = null;

let onBytes: ((chunk: Uint8Array) => void) | null = null;
let onDisconnect: (() => void) | null = null;

async function connect(): Promise<void> {
  port = await navigator.serial.requestPort();
  await port.open({ baudRate: BAUD_RATE });

  writer = port.writable!.getWriter();
  reader = (port.readable as ReadableStream<Uint8Array>).getReader();

  readBytes();

  port.addEventListener("disconnect", () => {
    cleanup();
    onDisconnect?.();
  });
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
};
