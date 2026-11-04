<script lang="ts">
  // Operator context menu (R-U2): Copy, Paste, Swap with OP n, EG copy to
  // OP n, Init operator.
  import { tick } from 'svelte';
  import { store } from '../store.svelte';
  import { send } from '../bridge';
  import { menus, opClipboard } from '../ui.svelte';
  import { INIT_OPERATOR, OP_BYTES, opBase } from '../params';

  const m = $derived(menus.op);
  let el: HTMLDivElement | undefined = $state();

  $effect(() => {
    if (m && el) void tick().then(() => el?.querySelector<HTMLButtonElement>('button:not([disabled])')?.focus());
  });

  function close(): void {
    menus.op = null;
  }
  function run(fn: () => void): void {
    fn();
    close();
  }
  function bytesOf(op: number): number[] {
    const b = opBase(op);
    return store.params.slice(b, b + OP_BYTES);
  }
  const others = $derived(m ? [1, 2, 3, 4, 5, 6].filter((n) => n !== m.op) : []);
  // Keep the menu inside the canvas.
  const left = $derived(m ? Math.min(m.x, 1280 - 170) : 0);
  const top = $derived(m ? Math.min(m.y, 1000 - 330) : 0);
</script>

{#if m}
  <div class="backdrop" role="presentation" onpointerdown={close} oncontextmenu={(e) => { e.preventDefault(); close(); }}></div>
  <div
    bind:this={el}
    class="menu"
    role="menu"
    aria-label="OP{m.op} menu"
    style="left:{left}px;top:{top}px"
    data-testid="op-menu"
  >
    <button tabindex="0" type="button" role="menuitem" data-testid="op-menu-copy" onclick={() =>
        run(() => {
          opClipboard.bytes = bytesOf(m.op);
        })}>Copy</button>
    <button tabindex="0"
      type="button"
      role="menuitem"
      disabled={!opClipboard.bytes}
      data-testid="op-menu-paste"
      onclick={() =>
        run(() => {
          if (opClipboard.bytes) send({ t: 'writeOp', op: m.op, bytes: [...opClipboard.bytes] });
        })}
      >Paste</button
    >
    <div class="sep" role="separator"></div>
    {#each others as n}
      <button tabindex="0" type="button" role="menuitem" data-testid="op-menu-swap-{n}" onclick={() => run(() => send({ t: 'swapOps', a: m.op, b: n }))}
        >Swap with OP {n}</button
      >
    {/each}
    <div class="sep" role="separator"></div>
    {#each others as n}
      <button tabindex="0" type="button" role="menuitem" data-testid="op-menu-egcopy-{n}" onclick={() => run(() => send({ t: 'egCopy', from: m.op, to: n }))}
        >EG copy to OP {n}</button
      >
    {/each}
    <div class="sep" role="separator"></div>
    <button tabindex="0"
      type="button"
      role="menuitem"
      data-testid="op-menu-init"
      onclick={() => run(() => send({ t: 'writeOp', op: m.op, bytes: [...INIT_OPERATOR] }))}>Init operator</button
    >
  </div>
{/if}
