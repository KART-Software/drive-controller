// Fused CRC16-CCITT + COBS encode in a single pass.
// Eliminates the intermediate plain-payload buffer: given serialised protobuf
// bytes, it simultaneously computes the CRC and COBS-encodes into the output,
// then appends the CRC (also COBS-encoded) and a 0x00 delimiter.

import { crc16Table } from "./crc16";

export function cobsCrcEncode(pb: Uint8Array): Uint8Array {
  // Worst-case output: COBS overhead + CRC(2) + delimiter(1)
  const maxOut = pb.length + 2 + Math.floor((pb.length + 2) / 254) + 2;
  const out = new Uint8Array(maxOut);

  let crc = 0xffff;
  let writeIdx = 1; // reserve first code byte
  let codeIdx = 0;
  let code = 1;

  function cobsByte(b: number): void {
    if (b === 0) {
      out[codeIdx] = code;
      codeIdx = writeIdx++;
      code = 1;
    } else {
      out[writeIdx++] = b;
      code++;
      if (code === 0xff) {
        out[codeIdx] = code;
        codeIdx = writeIdx++;
        code = 1;
      }
    }
  }

  // Feed protobuf bytes: CRC update + COBS encode
  for (let i = 0; i < pb.length; i++) {
    const b = pb[i];
    crc = ((crc << 8) ^ crc16Table[((crc >> 8) ^ b) & 0xff]) & 0xffff;
    cobsByte(b);
  }

  // Append CRC (LE) through COBS only
  cobsByte(crc & 0xff);
  cobsByte((crc >> 8) & 0xff);

  // Close final COBS block + delimiter
  out[codeIdx] = code;
  out[writeIdx++] = 0x00;

  return out.subarray(0, writeIdx);
}
