// Programmer name field rules (F-4.5).
export const NAME_LEN = 10;

type Segmenter = { segment(input: string): Iterable<{ segment: string }> };
type SegmenterCtor = new (locale?: string, opts?: { granularity: 'grapheme' }) => Segmenter;

/** Splits text into user-perceived characters (grapheme clusters where supported). */
function characters(text: string): string[] {
  const Ctor = (Intl as unknown as { Segmenter?: SegmenterCtor }).Segmenter;
  if (Ctor) return Array.from(new Ctor(undefined, { granularity: 'grapheme' }).segment(text), (s) => s.segment);
  return Array.from(text);
}

/**
 * Accepts 0x20–0x7E; every other character (non-ASCII, emoji, RTL, control)
 * becomes `?`. The result is truncated to 10 characters (pasted text).
 */
export function sanitizeName(text: string): string {
  let out = '';
  for (const ch of characters(text)) {
    if (out.length >= NAME_LEN) break;
    const c = ch.length === 1 ? ch.charCodeAt(0) : -1;
    out += c >= 0x20 && c <= 0x7e ? ch : '?';
  }
  return out;
}

/** Shorter names are padded with spaces to 10 characters. */
export function padName(text: string): string {
  return sanitizeName(text).padEnd(NAME_LEN, ' ');
}

/** The 10 character codes to store for a typed name. */
export function nameCodes(text: string): number[] {
  return Array.from(padName(text), (ch) => ch.charCodeAt(0));
}
