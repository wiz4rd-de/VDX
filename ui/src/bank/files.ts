// File import helpers (F-12): a chosen or dropped file goes to the core as
// base64 (`importFile`); the core scans and accepts or rejects it (R-D4).

/** Files above 1 MiB are rejected by the core; never read more than 1 MiB + 1 byte. */
export const MAX_READ = 1024 * 1024 + 1;

export function toBase64(bytes: Uint8Array): string {
  let s = '';
  const CHUNK = 0x8000;
  for (let i = 0; i < bytes.length; i += CHUNK) s += String.fromCharCode(...bytes.subarray(i, i + CHUNK));
  return btoa(s);
}

export async function readFileB64(file: Blob): Promise<string> {
  const buf = await file.slice(0, MAX_READ).arrayBuffer();
  return toBase64(new Uint8Array(buf));
}

/** True when a drag carries files from the OS. */
export function hasFiles(dt: DataTransfer | null): boolean {
  return !!dt && Array.from(dt.types).includes('Files');
}

export const VOICE_DRAG_TYPE = 'application/x-vdx-voice';

/** "Middle ellipsis" for long device names (§8): one line, full name in a tooltip. */
export function midEllipsis(s: string, max = 44): string {
  if (s.length <= max) return s;
  const head = Math.ceil((max - 1) / 2);
  const tail = Math.floor((max - 1) / 2);
  return `${s.slice(0, head)}…${s.slice(s.length - tail)}`;
}

/** Reason Send bank / Request dump are disabled (§5.7), or null when available. */
export function midiOutBlocked(env: { build: string; midi: string; output: string | null }): string | null {
  if (env.midi === 'unavailable') return 'MIDI output not available';
  if (env.midi === 'nosysex') return 'SysEx not permitted';
  if (env.build === 'web' && env.output === null) return 'MIDI output not available';
  return null;
}
