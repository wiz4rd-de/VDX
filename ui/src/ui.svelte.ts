// UI-only state that is not part of the core snapshot.

/** Canvas scale (1 = 1280 × 1000 logical px at 100 %). */
export const view = $state({ scale: 1 });

/** Open popup menus (R-U2 operator menu, R-U3 voice menu). Coordinates are canvas px. */
export const menus = $state({
  op: null as null | { op: number; x: number; y: number },
  voice: false,
});

/** R-U2 in-app operator clipboard (per process): 21 bytes, offsets 0..20. */
export const opClipboard = $state({ bytes: null as number[] | null });

/** Pending New/Eject confirmation in the bank manager (F-12.3/4). */
export const bankConfirm = $state({ action: null as null | 'new' | 'eject' });
