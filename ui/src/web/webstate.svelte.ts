// Web page states (SPEC §5.9), driven by the web glue. The start card and the
// fatal error screen are shown through this API; "No audio output available."
// and the IndexedDB banner follow the `env` event (`audio`, `idb`).

export const webState = $state({
  /** The "Click or press any key to start VDX" card is visible. */
  startCard: false,
  /** Full-screen error message replacing the UI, or null. */
  fatal: null as string | null,
});

let resolveStart: (() => void) | null = null;

/**
 * Shows the start card. The returned promise resolves on the dismissing
 * click or key, which is not passed on to any control or to the keyboard; the
 * glue starts audio and the splash in its handler (it runs inside the user
 * gesture).
 */
export function showStartCard(onGesture?: () => void): Promise<void> {
  webState.startCard = true;
  return new Promise((resolve) => {
    resolveStart = () => {
      onGesture?.();
      resolve();
    };
  });
}

/** Dismisses the start card (called by the UI on the first click or key). */
export function dismissStartCard(): void {
  if (!webState.startCard) return;
  webState.startCard = false;
  const r = resolveStart;
  resolveStart = null;
  r?.();
}

/** Replaces the UI with a centred message naming the problem and the fix. */
export function showFatalError(message: string): void {
  webState.fatal = message;
}
