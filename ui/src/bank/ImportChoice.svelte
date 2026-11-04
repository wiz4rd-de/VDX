<script lang="ts">
  // Bank import choice (F-12.1/2): "Insert as cartridge" (default; Enter
  // confirms) or "Load into internal".
  import { tick } from 'svelte';
  import { store } from '../store.svelte';
  import { send } from '../bridge';

  let insertBtn: HTMLButtonElement | undefined = $state();
  $effect(() => {
    if (insertBtn) void tick().then(() => insertBtn?.focus());
  });

  function choose(target: 'crt' | 'int'): void {
    send({ t: 'importChoice', target });
    store.importChoice = null;
  }
  function cancel(): void {
    send({ t: 'importCancel' });
    store.importChoice = null;
  }
  function onKey(ev: KeyboardEvent): void {
    // Enter confirms the default unless a button has focus (it then activates that button).
    if (ev.key === 'Enter' && !(ev.target instanceof HTMLButtonElement)) {
      ev.preventDefault();
      choose('crt');
    }
  }
</script>

<div class="scrim top" role="presentation"></div>
<div
  class="overlay choice"
  role="dialog"
  aria-modal="true"
  aria-label="Bank file"
  tabindex="-1"
  data-testid="import-choice"
  onkeydown={onKey}
>
  <p class="msg">Bank file “{store.importChoice?.name}”</p>
  <div class="btns">
    <button tabindex="0" bind:this={insertBtn} type="button" class="btn primary" data-testid="import-crt" onclick={() => choose('crt')}
      >Insert as cartridge</button
    >
    <button tabindex="0" type="button" class="btn" data-testid="import-int" onclick={() => choose('int')}>Load into internal</button>
    <button tabindex="0" type="button" class="btn" data-testid="import-cancel" onclick={cancel}>Cancel</button>
  </div>
</div>

<style>
  .choice {
    left: 440px;
    top: 420px;
    width: 400px;
    height: 120px;
    z-index: 30;
  }
  .scrim.top {
    z-index: 29;
  }
  .msg {
    margin: 22px 20px 18px;
    font: 600 15px/1.2 'Barlow Condensed', sans-serif;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .btns {
    display: flex;
    gap: 8px;
    padding: 0 20px;
  }
</style>
