export interface Transport {
  /** chooser: 必ずポート選択ダイアログを出す。auto: ダイアログを出さず、許可済みが 1 台のときだけつなぐ (つながなければ false)。
   *  既定は許可済みが 1 台なら自動、それ以外はダイアログ。接続したら true */
  connect(options?: { chooser?: boolean; auto?: boolean }): Promise<boolean>;
  disconnect(): Promise<void>;
  /** Send raw bytes to the wire (no framing applied). */
  send(bytes: Uint8Array): Promise<void>;
  isConnected(): boolean;
  /** Called on every raw chunk received from the wire. */
  setOnBytes(fn: (chunk: Uint8Array) => void): void;
  setOnDisconnect(fn: () => void): void;
  /** 接続中のデバイスの USB VID / PID (分からなければ null)。drive-controller (0x0483) と SD リーダー (0x04D5) の見分けに使う */
  deviceInfo(): { usbVendorId?: number; usbProductId?: number } | null;
  /** 許可済みのデバイスが USB に挿されたときに呼ぶ (自動接続のきっかけ) */
  setOnDeviceAttached(fn: () => void): void;
}
