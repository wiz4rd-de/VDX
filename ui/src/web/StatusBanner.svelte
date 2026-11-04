<script lang="ts">
  // §5.9 / F-15.3 web notices: "No audio output available." and the one-line
  // banner "Changes will not be saved in this browser.", from the `env` event.
  import { store } from '../store.svelte';

  const lines = $derived.by(() => {
    if (!store.envKnown || store.env.build !== 'web') return [];
    const out: string[] = [];
    if (!store.env.audio) out.push('No audio output available.');
    if (!store.env.idb) out.push('Changes will not be saved in this browser.');
    return out;
  });
</script>

{#if lines.length > 0}
  <div class="banner" role="status" data-testid="web-banner">
    {#each lines as l}<span>{l}</span>{/each}
  </div>
{/if}

<style>
  .banner {
    position: absolute;
    left: 712px;
    top: 14px;
    width: 464px;
    height: 32px;
    display: flex;
    flex-direction: column;
    align-items: flex-end;
    justify-content: center;
    gap: 2px;
    color: var(--legend);
    font: 600 12px/1 'Barlow Condensed', sans-serif;
    letter-spacing: 0.04em;
    pointer-events: none;
  }
</style>
