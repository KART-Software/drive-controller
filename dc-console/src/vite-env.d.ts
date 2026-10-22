/// <reference types="vite/client" />

// Web Serial API types (not yet in lib.dom.d.ts)
interface SerialPort extends EventTarget {
  readonly readable: ReadableStream<Uint8Array>;
  readonly writable: WritableStream<Uint8Array>;
  open(options: { baudRate: number }): Promise<void>;
  close(): Promise<void>;
  setSignals(signals: { dataTerminalReady?: boolean; requestToSend?: boolean; break?: boolean }): Promise<void>;
  getInfo(): { usbVendorId?: number; usbProductId?: number };
  addEventListener(type: "disconnect", listener: () => void): void;
}

interface Serial extends EventTarget {
  addEventListener(type: "connect" | "disconnect", listener: (e: Event) => void): void;
  requestPort(options?: { filters?: { usbVendorId?: number; usbProductId?: number }[] }): Promise<SerialPort>;
  getPorts(): Promise<SerialPort[]>;
}

interface Navigator {
  readonly serial: Serial;
}
