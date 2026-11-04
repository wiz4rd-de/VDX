<script lang="ts">
  // Programmer name field (F-4.5): accepts 0x20–0x7E, everything else becomes
  // `?`; pasted text is truncated to 10 characters, shorter names are padded
  // with spaces. Typing here never plays notes (it is a text field, R-K2).
  import { store } from '../store.svelte';
  import { send } from '../bridge';
  import { G } from '../params';
  import { sanitizeName, nameCodes, NAME_LEN } from '../name';

  let el: HTMLInputElement | undefined = $state();
  let focused = $state(false);
  let text = $state('');

  const coreName = $derived(store.ready ? store.voiceName : '');

  $effect(() => {
    // Follow the core while the field is not being edited.
    const n = coreName;
    if (!focused) text = n.replace(/ +$/, '');
  });

  function apply(raw: string): void {
    const clean = sanitizeName(raw);
    if (el && el.value !== clean) {
      const pos = Math.min(el.selectionStart ?? clean.length, clean.length);
      el.value = clean;
      el.setSelectionRange(pos, pos);
    }
    text = clean;
    const codes = nameCodes(clean);
    for (let k = 0; k < NAME_LEN; k++) {
      if (store.params[G.NAME + k] !== codes[k]) send({ t: 'setParam', i: G.NAME + k, v: codes[k] });
    }
  }

  function onKey(ev: KeyboardEvent): void {
    if (ev.key === 'Enter' || ev.key === 'Escape') {
      ev.preventDefault();
      ev.stopPropagation();
      el?.blur();
    }
  }
</script>

<input
  bind:this={el}
  class="name"
  type="text"
  value={text}
  spellcheck="false"
  autocomplete="off"
  disabled={!store.ready}
  aria-label="Voice Name"
  title="Voice Name (10 characters, 0x20–0x7E)"
  data-testid="name-field"
  oninput={(ev) => apply((ev.currentTarget as HTMLInputElement).value)}
  onfocus={() => (focused = true)}
  onblur={() => {
    focused = false;
    text = coreName.replace(/ +$/, '');
  }}
  onkeydown={onKey}
/>

<style>
  .name {
    box-sizing: border-box;
    width: 100%;
    height: 22px;
    padding: 0 6px;
    border: 1px solid var(--panel);
    border-radius: 2px;
    background: var(--body);
    color: var(--legend);
    font: 600 14px/1 'Barlow Condensed', sans-serif;
    letter-spacing: 0.12em;
  }
  .name:disabled {
    opacity: 1;
  }
</style>
