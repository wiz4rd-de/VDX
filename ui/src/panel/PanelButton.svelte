<script lang="ts">
  // A front-panel membrane button: `button` down on press, up on release
  // (including a release outside the window; YES/NO auto-repeat is the core's).
  import type { Snippet } from 'svelte';
  import { send } from '../bridge';

  interface Props {
    id: number;
    x: number;
    y: number;
    w: number;
    h: number;
    label: string;
    lit?: boolean;
    /** The lamp is blinking (400 ms on / off); `lit` already follows the phase. */
    blink?: boolean;
    salmon?: boolean;
    testid?: string;
    children?: Snippet;
  }
  let { id, x, y, w, h, label, lit = false, blink = false, salmon = false, testid, children }: Props = $props();

  let pressed = $state(false);

  function press(): void {
    if (pressed) return;
    pressed = true;
    send({ t: 'button', id, down: true });
  }
  function release(): void {
    if (!pressed) return;
    pressed = false;
    send({ t: 'button', id, down: false });
  }
  function onPointerDown(ev: PointerEvent): void {
    if (ev.button !== 0) return;
    (ev.currentTarget as HTMLElement).setPointerCapture?.(ev.pointerId);
    press();
  }
  function onKeyDown(ev: KeyboardEvent): void {
    if (ev.defaultPrevented || ev.repeat || ev.altKey || ev.ctrlKey || ev.metaKey) return;
    if (ev.key === 'Enter' || ev.key === ' ') {
      ev.preventDefault();
      press();
    }
  }
  function onKeyUp(ev: KeyboardEvent): void {
    if (ev.key === 'Enter' || ev.key === ' ') {
      if (pressed) ev.preventDefault();
      release();
    }
  }
</script>

<button tabindex="0"
  type="button"
  class="pb"
  class:lit
  class:salmon
  class:pressed
  style="left:{x}px;top:{y}px;width:{w}px;height:{h}px"
  aria-label={label}
  data-testid={testid ?? `btn-${id}`}
  data-lit={lit ? '1' : '0'}
  data-blink={blink ? '1' : '0'}
  onpointerdown={onPointerDown}
  onpointerup={release}
  onpointercancel={release}
  onlostpointercapture={release}
  onkeydown={onKeyDown}
  onkeyup={onKeyUp}
  onblur={release}
  oncontextmenu={(ev) => ev.preventDefault()}
>
  {@render children?.()}
</button>

<style>
  .pb {
    position: absolute;
    box-sizing: border-box;
    margin: 0;
    padding: 0;
    border: none;
    border-radius: 2px;
    background: var(--dx-green);
    color: var(--legend);
    font: 600 10px/1 'Barlow Condensed', sans-serif;
    font-variant-numeric: tabular-nums;
    cursor: pointer;
    touch-action: none;
    user-select: none;
  }
  .pb.lit {
    background: var(--dx-green-lit);
  }
  .pb.salmon {
    background: var(--salmon);
  }
  .pb.salmon.lit {
    background: var(--salmon);
    box-shadow: inset 0 0 0 2px var(--legend);
  }
  .pb.pressed {
    transform: translateY(1px);
  }
</style>
