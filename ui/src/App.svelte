<script lang="ts">
  import { onMount } from 'svelte';
  import { store } from './store.svelte';
  import { send } from './bridge';
  import { view } from './ui.svelte';
  import { keyboard, installGlobalInput } from './input';
  import { webState } from './web/webstate.svelte';
  import { fitScale, CANVAS_W, CANVAS_H } from './scale';
  import { hasFiles, readFileB64 } from './bank/files';
  import FrontPanel from './panel/FrontPanel.svelte';
  import Programmer from './programmer/Programmer.svelte';
  import OpMenu from './programmer/OpMenu.svelte';
  import VoiceMenu from './programmer/VoiceMenu.svelte';
  import BankManager from './bank/BankManager.svelte';
  import ImportChoice from './bank/ImportChoice.svelte';
  import Settings from './settings/Settings.svelte';
  import StartCard from './web/StartCard.svelte';
  import FatalError from './web/FatalError.svelte';
  import StatusBanner from './web/StatusBanner.svelte';

  let winW = $state(typeof window !== 'undefined' ? window.innerWidth : CANVAS_W);
  let winH = $state(typeof window !== 'undefined' ? window.innerHeight : CANVAS_H);
  const scale = $derived(fitScale(winW, winH, store.env.build, store.settings.zoom));

  $effect(() => {
    view.scale = scale;
  });

  // Computer keyboard play state: the core's setting once known, else the build default (R-K2).
  $effect(() => {
    const on = store.keyboardPlayKnown ? store.settings.keyboardPlay === 1 : store.env.build !== 'plugin';
    keyboard.setPlay(on);
  });

  async function importDropped(file: File): Promise<void> {
    const b64 = await readFileB64(file);
    send({ t: 'importFile', name: file.name, b64 });
  }

  onMount(() => {
    installGlobalInput();
    const onResize = (): void => {
      winW = window.innerWidth;
      winH = window.innerHeight;
    };
    window.addEventListener('resize', onResize);
    const blink = setInterval(() => (store.blinkOn = !store.blinkOn), 400);
    // F-12.2: a .syx dropped anywhere on the window is imported like Load file….
    const onDragOver = (ev: DragEvent): void => {
      if (hasFiles(ev.dataTransfer)) {
        ev.preventDefault();
        if (ev.dataTransfer) ev.dataTransfer.dropEffect = 'copy';
      }
    };
    const onDrop = (ev: DragEvent): void => {
      if (!hasFiles(ev.dataTransfer)) return;
      ev.preventDefault();
      const file = ev.dataTransfer?.files[0];
      if (file) void importDropped(file);
    };
    window.addEventListener('dragover', onDragOver);
    window.addEventListener('drop', onDrop);
    send({ t: 'uiReady' });
    return () => {
      window.removeEventListener('resize', onResize);
      window.removeEventListener('dragover', onDragOver);
      window.removeEventListener('drop', onDrop);
      clearInterval(blink);
    };
  });
</script>

{#if webState.fatal}
  <FatalError message={webState.fatal} />
{:else}
  <div class="viewport" data-testid="viewport">
    <div class="stage" style="width:{CANVAS_W * scale}px;height:{CANVAS_H * scale}px">
      <div class="canvas" data-canvas data-testid="canvas" data-scale={scale} style="transform:scale({scale})">
        <div class="prog-bg"></div>
        <FrontPanel />
        <StatusBanner />
        <Programmer />
        <OpMenu />
        <VoiceMenu />
        {#if store.overlay === 'bank'}<BankManager />{/if}
        {#if store.overlay === 'settings'}<Settings />{/if}
        {#if store.importChoice}<ImportChoice />{/if}
      </div>
    </div>
  </div>
  {#if webState.startCard}<StartCard />{/if}
{/if}

<style>
  .viewport {
    position: fixed;
    inset: 0;
    overflow: auto;
    display: flex;
    background: var(--body);
  }
  .stage {
    position: relative;
    flex: none;
    margin: auto;
    overflow: hidden;
  }
  .canvas {
    position: absolute;
    left: 0;
    top: 0;
    width: 1280px;
    height: 1000px;
    transform-origin: 0 0;
    background: var(--body);
    overflow: hidden;
  }
  .prog-bg {
    position: absolute;
    left: 0;
    top: 270px;
    width: 1280px;
    height: 730px;
    background: var(--programmer);
  }
</style>
