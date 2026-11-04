// SysEx and other test files for the bank-manager suites (F-12, R-D4), built
// from banks/vdx-default.syx and synthesized bytes (§4.6, §4.7).
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const DEFAULT_BANK = fileURLToPath(new URL('../../../banks/vdx-default.syx', import.meta.url));

export const checksum = (data: Uint8Array | number[]): number => {
  let sum = 0;
  for (const b of data) sum += b;
  return -sum & 0x7f;
};

/** The committed default bank (4104 bytes). */
export function defaultBank(): Buffer {
  return readFileSync(DEFAULT_BANK);
}

const ascii10 = (name: string): number[] => Array.from(name.padEnd(10).slice(0, 10), (c) => c.charCodeAt(0));

/** A 4104-byte bank: the default bank with the given slot names (packed bytes 118–127). */
export function bankWithNames(names: string[]): Buffer {
  const b = Buffer.from(defaultBank());
  for (let s = 0; s < 32; s++) {
    const off = 6 + s * 128 + 118;
    ascii10(names[s] ?? `SLOT ${s + 1}`).forEach((c, k) => (b[off + k] = c));
  }
  b[4102] = checksum(b.subarray(6, 4102));
  b[4103] = 0xf7;
  return b;
}

/** The headerless packed 4096-byte form of a bank (R-D4). */
export function headerless(bank: Buffer): Buffer {
  return Buffer.from(bank.subarray(6, 4102));
}

/** Names E2E 01 … E2E 32 for a recognisable bank. */
export const e2eNames = (tag: string): string[] => Array.from({ length: 32 }, (_, i) => `${tag} ${String(i + 1).padStart(2, '0')}`);

/** A 163-byte single-voice dump: INIT VOICE values (R-P10) with a name and algorithm. */
export function voiceDump(name: string, algorithm = 1): Buffer {
  const v: number[] = [];
  for (let op = 6; op >= 1; op--) {
    v.push(99, 99, 99, 99, 99, 99, 99, 0); // rates, levels
    v.push(0, 0, 0, 0, 0, 0, 0, 0); // BP, depths, curves, RS, AMS, KVS
    v.push(op === 1 ? 99 : 0, 0, 1, 0, 7); // OL, mode, coarse, fine, detune
  }
  v.push(99, 99, 99, 99, 50, 50, 50, 50); // pitch EG
  v.push(algorithm - 1, 0, 1, 35, 0, 0, 0, 1, 0, 3, 24); // alg, fb, oks, lfo…, pms, transpose
  v.push(...ascii10(name));
  if (v.length !== 155) throw new Error(`voice has ${v.length} bytes`);
  return Buffer.from([0xf0, 0x43, 0x00, 0x00, 0x01, 0x1b, ...v, checksum(v), 0xf7]);
}

/** A small WAV file (rejected by the import scan). */
export function wav(): Buffer {
  const data = Buffer.alloc(800);
  const h = Buffer.alloc(44);
  h.write('RIFF', 0);
  h.writeUInt32LE(36 + data.length, 4);
  h.write('WAVEfmt ', 8);
  h.writeUInt32LE(16, 16);
  h.writeUInt16LE(1, 20);
  h.writeUInt16LE(1, 22);
  h.writeUInt32LE(8000, 24);
  h.writeUInt32LE(16000, 28);
  h.writeUInt16LE(2, 32);
  h.writeUInt16LE(16, 34);
  h.write('data', 36);
  h.writeUInt32LE(data.length, 40);
  return Buffer.concat([h, data]);
}
