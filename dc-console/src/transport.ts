export interface Transport {
  connect(): Promise<void>;
  disconnect(): Promise<void>;
  /** Send raw bytes to the wire (no framing applied). */
  send(bytes: Uint8Array): Promise<void>;
  isConnected(): boolean;
  /** Called on every raw chunk received from the wire. */
  setOnBytes(fn: (chunk: Uint8Array) => void): void;
  setOnDisconnect(fn: () => void): void;
}
