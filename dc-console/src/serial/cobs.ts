// Consistent Overhead Byte Stuffing (COBS) encode/decode.
// Frames on the wire are: COBS(payload) followed by a single 0x00 delimiter.

export function cobsEncode(src: Uint8Array): Uint8Array {
  const out = new Uint8Array(src.length + Math.floor(src.length / 254) + 2);
  let readIndex = 0;
  let writeIndex = 1;
  let codeIndex = 0;
  let code = 1;

  while (readIndex < src.length) {
    if (src[readIndex] === 0) {
      out[codeIndex] = code;
      code = 1;
      codeIndex = writeIndex++;
      readIndex++;
    } else {
      out[writeIndex++] = src[readIndex++];
      code++;
      if (code === 0xff) {
        out[codeIndex] = code;
        code = 1;
        codeIndex = writeIndex++;
      }
    }
  }
  out[codeIndex] = code;
  return out.subarray(0, writeIndex);
}

// Decode a single COBS-encoded chunk (without the trailing 0x00 delimiter).
// Returns null on malformed input.
export function cobsDecode(src: Uint8Array): Uint8Array | null {
  if (src.length === 0) return null;
  const out = new Uint8Array(src.length);
  let readIndex = 0;
  let writeIndex = 0;

  while (readIndex < src.length) {
    const code = src[readIndex];
    if (code === 0 || readIndex + code > src.length) {
      return null;
    }
    readIndex++;
    for (let i = 1; i < code; i++) {
      out[writeIndex++] = src[readIndex++];
    }
    if (code !== 0xff && readIndex < src.length) {
      out[writeIndex++] = 0;
    }
  }
  return out.subarray(0, writeIndex);
}

// Streaming framer: feed bytes in any chunk size, get whole COBS-decoded
// payloads via the onFrame callback. Frames are split on 0x00 bytes.
export class FrameDecoder {
  private buf: number[] = [];

  constructor(private onFrame: (payload: Uint8Array) => void) {}

  push(chunk: Uint8Array): void {
    for (let i = 0; i < chunk.length; i++) {
      const byte = chunk[i];
      if (byte === 0x00) {
        if (this.buf.length > 0) {
          const decoded = cobsDecode(Uint8Array.from(this.buf));
          this.buf.length = 0;
          if (decoded) {
            this.onFrame(decoded);
          }
        }
      } else {
        this.buf.push(byte);
      }
    }
  }

  reset(): void {
    this.buf.length = 0;
  }
}
