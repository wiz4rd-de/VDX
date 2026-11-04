<script lang="ts">
  // Bank manager overlay (§5.7, F-12): 960 × 640, centred.
  import { tick } from 'svelte';
  import { store } from '../store.svelte';
  import { send } from '../bridge';
  import { bankConfirm } from '../ui.svelte';
  import { VOICE_DRAG_TYPE, midiOutBlocked } from './files';

  const X = 160;
  const Y = 180;
  let root: HTMLDivElement | undefined = $state();
  let dropRow = $state<string | null>(null);
  let dropEdit = $state(false);

  $effect(() => {
    if (root) void tick().then(() => root?.querySelector<HTMLElement>('[data-autofocus]')?.focus());
  });

  const ready = $derived(store.ready);
  const crt = $derived(store.cartridge);
  const crtTitle = $derived(crt.present ? `${crt.name}${crt.changed ? ' •' : ''}` : 'NO CARTRIDGE');
  const blocked = $derived(midiOutBlocked(store.env));
  const status = $derived.by(() => {
    if (store.bankNotice) return store.bankNotice.text;
    if (store.dumpWait === 'waiting') return 'Waiting for bank…';
    if (store.dumpWait === 'noReply') return 'No reply.';
    return '';
  });

  function close(): void {
    bankConfirm.action = null;
    if (store.dumpWait === 'noReply') store.dumpWait = 'idle';
    store.overlay = 'none';
  }

  function slotName(bank: number, slot: number): string {
    return ready ? store.names[bank][slot - 1] : '';
  }

  function onDragStart(ev: DragEvent, bank: number, slot: number): void {
    ev.dataTransfer?.setData(VOICE_DRAG_TYPE, JSON.stringify({ bank, slot }));
    if (ev.dataTransfer) ev.dataTransfer.effectAllowed = 'copy';
  }
  function dragged(ev: DragEvent): { bank: number; slot: number } | null {
    const raw = ev.dataTransfer?.getData(VOICE_DRAG_TYPE);
    if (!raw) return null;
    try {
      return JSON.parse(raw) as { bank: number; slot: number };
    } catch {
      return null;
    }
  }
  function isVoiceDrag(ev: DragEvent): boolean {
    return !!ev.dataTransfer && Array.from(ev.dataTransfer.types).includes(VOICE_DRAG_TYPE);
  }
  function onRowOver(ev: DragEvent, key: string): void {
    if (!isVoiceDrag(ev)) return;
    ev.preventDefault();
    if (ev.dataTransfer) ev.dataTransfer.dropEffect = 'copy';
    dropRow = key;
  }
  function onRowDrop(ev: DragEvent, bank: number, slot: number): void {
    dropRow = null;
    const src = dragged(ev);
    if (!src) return;
    ev.preventDefault();
    ev.stopPropagation();
    if (src.bank === bank && src.slot === slot) return;
    send({ t: 'copyVoice', fromBank: src.bank, fromSlot: src.slot, toBank: bank, toSlot: slot });
  }
  function onEditOver(ev: DragEvent): void {
    if (!isVoiceDrag(ev)) return;
    ev.preventDefault();
    dropEdit = true;
  }
  function onEditDrop(ev: DragEvent): void {
    dropEdit = false;
    const src = dragged(ev);
    if (!src) return;
    ev.preventDefault();
    ev.stopPropagation();
    send({ t: 'selectVoice', bank: src.bank, slot: src.slot });
  }

  function newCartridge(): void {
    if (crt.present && crt.changed) bankConfirm.action = 'new';
    else send({ t: 'newCartridge' });
  }
  function eject(): void {
    if (crt.present && crt.changed) bankConfirm.action = 'eject';
    else send({ t: 'ejectCartridge' });
  }
  function confirmReplace(): void {
    send({ t: bankConfirm.action === 'eject' ? 'ejectCartridge' : 'newCartridge' });
    bankConfirm.action = null;
  }
  function requestDump(): void {
    send({ t: 'requestDump' });
    store.startDumpWait();
  }
  function toggleProtect(bank: 0 | 1): void {
    const id = bank === 0 ? 'protectInt' : 'protectCrt';
    send({ t: 'setGlobal', id, v: store.globals[id].v ? 0 : 1 });
  }
</script>

<div class="scrim" role="presentation" onpointerdown={close} data-testid="bank-scrim"></div>
<div
  bind:this={root}
  class="overlay"
  style="left:{X}px;top:{Y}px;width:960px;height:640px"
  role="dialog"
  aria-modal="true"
  aria-label="Bank manager"
  data-testid="bank-manager"
>
  {#each [0, 1] as bank}
    {@const isCrt = bank === 1}
    {@const protectId = bank === 0 ? 'protectInt' : 'protectCrt'}
    <section class="col" style="left:{16 + bank * 472}px" aria-label={isCrt ? 'Cartridge' : 'Internal'}>
      <header class="colhead">
        <span class="bank-name" data-testid="bank-name-{bank}" title={isCrt ? crt.name : 'INTERNAL'}
          >{isCrt ? crtTitle : 'INTERNAL'}</span
        >
        <button tabindex="0"
          type="button"
          class="tog"
          class:on={store.globals[protectId].v === 1}
          role="switch"
          aria-checked={store.globals[protectId].v === 1}
          aria-label={isCrt ? 'Memory protect cartridge' : 'Memory protect internal'}
          data-testid="bank-protect-{bank}"
          onclick={() => toggleProtect(bank as 0 | 1)}>PROTECT {store.globals[protectId].v ? 'ON' : 'OFF'}</button
        >
        <button tabindex="0"
          type="button"
          class="btn"
          disabled={isCrt && !crt.present}
          data-testid="bank-export-{bank}"
          onclick={() => send({ t: 'export', what: isCrt ? 'crt' : 'int' })}>Export…</button
        >
      </header>
      {#if isCrt && !crt.present}
        <div class="empty">
          <span>NO CARTRIDGE</span>
          <button tabindex="0" type="button" class="btn" data-testid="bank-load-empty" onclick={() => send({ t: 'pickFile' })}>Load file…</button>
        </div>
      {:else}
        <ol class="rows" aria-label={isCrt ? 'Cartridge voices' : 'Internal voices'}>
          {#each Array.from({ length: 32 }, (_, i) => i + 1) as slot}
            {@const key = `${bank}:${slot}`}
            <li>
              <button tabindex="0"
                type="button"
                class="row"
                class:drop={dropRow === key}
                class:source={ready && store.memory.srcBank === bank && store.memory.srcSlot === slot}
                draggable="true"
                aria-label="{isCrt ? 'CRT' : 'INT'} {slot} {slotName(bank, slot)}"
                data-testid="bank-row-{bank}-{slot}"
                ondragstart={(ev) => onDragStart(ev, bank, slot)}
                ondragover={(ev) => onRowOver(ev, key)}
                ondragleave={() => (dropRow = dropRow === key ? null : dropRow)}
                ondrop={(ev) => onRowDrop(ev, bank, slot)}
                ondblclick={() => send({ t: 'selectVoice', bank, slot })}
                onkeydown={(ev) => {
                  if (ev.key === 'Enter' && !ev.repeat) {
                    ev.preventDefault();
                    send({ t: 'selectVoice', bank, slot });
                  }
                }}
              >
                <span class="slot">{String(slot).padStart(2, ' ')}</span>
                <span class="nm">{slotName(bank, slot)}</span>
              </button>
            </li>
          {/each}
        </ol>
      {/if}
    </section>
  {/each}

  <div
    class="editbuf"
    class:drop={dropEdit}
    role="region"
    aria-label="Edit buffer"
    data-testid="bank-edit-buffer"
    ondragover={onEditOver}
    ondragleave={() => (dropEdit = false)}
    ondrop={onEditDrop}
  >
    <span class="lbl">EDIT BUFFER</span>
    <span class="ebname">{ready ? store.voiceName : ''}</span>
  </div>

  <div class="status" role="status" aria-live="polite" data-testid="bank-status">
    {#if bankConfirm.action}
      <span>Replace the current cartridge? Unsaved changes will be lost.</span>
      <button tabindex="0" type="button" class="btn" data-testid="bank-confirm-replace" onclick={confirmReplace}>Replace</button>
      <button tabindex="0" type="button" class="btn" data-testid="bank-confirm-cancel" data-autofocus onclick={() => (bankConfirm.action = null)}
        >Cancel</button
      >
    {:else}
      <span>{status}</span>
    {/if}
  </div>

  <div class="buttons">
    <button tabindex="0" type="button" class="btn" data-autofocus data-testid="bank-load" onclick={() => send({ t: 'pickFile' })}>Load file…</button>
    <button tabindex="0" type="button" class="btn" data-testid="bank-new" onclick={newCartridge}>New cartridge</button>
    <button tabindex="0" type="button" class="btn" data-testid="bank-eject" disabled={!crt.present} onclick={eject}>Eject cartridge</button>
    <button tabindex="0" type="button" class="btn" data-testid="bank-export-voice" onclick={() => send({ t: 'export', what: 'voice' })}
      >Export voice…</button
    >
    <span class="wrap" title={blocked ?? ''}>
      <button tabindex="0" type="button" class="btn" data-testid="bank-send" disabled={blocked !== null} onclick={() => send({ t: 'sendBank' })}
        >Send bank (MIDI)</button
      >
    </span>
    <span class="wrap" title={blocked ?? ''}>
      <button tabindex="0" type="button" class="btn" data-testid="bank-request" disabled={blocked !== null} onclick={requestDump}
        >Request dump (MIDI)</button
      >
    </span>
    <button tabindex="0" type="button" class="btn" data-testid="bank-close" onclick={close}>Close</button>
  </div>
</div>

<style>
  .col {
    position: absolute;
    top: 16px;
    width: 456px;
  }
  .colhead {
    display: flex;
    align-items: center;
    gap: 8px;
    height: 28px;
  }
  .bank-name {
    flex: 1 1 auto;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    font: 600 15px/1 'Barlow Condensed', sans-serif;
    letter-spacing: 0.08em;
  }
  .rows {
    list-style: none;
    margin: 8px 0 0;
    padding: 0;
    border: 1px solid var(--panel);
    border-radius: 2px;
  }
  .row {
    display: flex;
    width: 100%;
    height: 13px;
    margin: 0;
    padding: 0 8px;
    gap: 10px;
    align-items: center;
    border: none;
    background: transparent;
    color: var(--legend);
    font: 500 12px/13px 'Barlow Condensed', sans-serif;
    font-variant-numeric: tabular-nums;
    text-align: left;
    cursor: grab;
  }
  li:nth-child(odd) .row {
    background: var(--body);
  }
  .row.source {
    color: var(--dx-green-lit);
  }
  .row.drop,
  .editbuf.drop {
    outline: 2px dashed var(--legend);
    outline-offset: -2px;
  }
  .slot {
    width: 16px;
    text-align: right;
    white-space: pre;
  }
  .nm {
    white-space: pre;
    letter-spacing: 0.08em;
  }
  .empty {
    margin-top: 8px;
    height: 416px;
    border: 1px solid var(--panel);
    border-radius: 2px;
    display: flex;
    flex-direction: column;
    gap: 14px;
    align-items: center;
    justify-content: center;
    font: 600 15px/1 'Barlow Condensed', sans-serif;
    letter-spacing: 0.1em;
  }
  .editbuf {
    position: absolute;
    left: 16px;
    right: 16px;
    top: 484px;
    height: 36px;
    display: flex;
    align-items: center;
    gap: 14px;
    padding: 0 12px;
    border: 1px dashed var(--brown-legend);
    border-radius: 3px;
  }
  .ebname {
    font: 600 15px/1 'Barlow Condensed', sans-serif;
    letter-spacing: 0.12em;
    white-space: pre;
  }
  .status {
    position: absolute;
    left: 16px;
    right: 16px;
    top: 532px;
    height: 32px;
    display: flex;
    align-items: center;
    gap: 10px;
    font: 500 14px/1 'Barlow Condensed', sans-serif;
  }
  .buttons {
    position: absolute;
    left: 16px;
    right: 16px;
    bottom: 16px;
    display: flex;
    gap: 8px;
    justify-content: space-between;
  }
  .wrap {
    display: inline-flex;
  }
</style>
