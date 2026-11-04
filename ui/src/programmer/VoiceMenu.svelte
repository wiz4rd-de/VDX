<script lang="ts">
  // Voice menu (R-U3): Init voice, Compare (disabled while CLEAN), Randomise.
  import { tick } from 'svelte';
  import { store } from '../store.svelte';
  import { send } from '../bridge';
  import { menus } from '../ui.svelte';

  let el: HTMLDivElement | undefined = $state();
  const edited = $derived(store.memory.edited || store.panel.edited || store.panel.comparing);

  $effect(() => {
    if (menus.voice && el) void tick().then(() => el?.querySelector<HTMLButtonElement>('button:not([disabled])')?.focus());
  });

  function run(fn: () => void): void {
    fn();
    menus.voice = false;
  }
</script>

{#if menus.voice}
  <div class="backdrop" role="presentation" onpointerdown={() => (menus.voice = false)}></div>
  <div bind:this={el} class="menu" role="menu" aria-label="Voice menu" style="left:744px;top:298px" data-testid="voice-menu">
    <button tabindex="0" type="button" role="menuitem" data-testid="voice-menu-init" onclick={() => run(() => send({ t: 'initVoice' }))}>Init voice</button>
    <button tabindex="0"
      type="button"
      role="menuitemcheckbox"
      aria-checked={store.panel.comparing}
      disabled={!edited}
      data-testid="voice-menu-compare"
      onclick={() => run(() => send({ t: 'compare' }))}>Compare</button
    >
    <button tabindex="0"
      type="button"
      role="menuitem"
      data-testid="voice-menu-randomise"
      onclick={() => run(() => send({ t: 'randomise', seed: Date.now() & 0x7fffffff }))}>Randomise</button
    >
  </div>
{/if}
