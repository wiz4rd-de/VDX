// Production entry: one bundle for every build (§9.4). Inside a JUCE web view
// (standalone, plugin) the JUCE backend connects at once; the web build's glue
// installs the WASM backend with setBackend() (bridge.ts).
import { boot } from './boot';
import { createJuceBackend, setBackend } from './bridge';
import { store } from './store.svelte';
import { startWeb } from './wasm/glue';

const juce = createJuceBackend();
if (juce) {
  store.env.build = 'standalone';
  setBackend(juce);
}
boot();
// The web build: load the WASM core, restore state, show the start card (§5.9).
if (!juce) void startWeb({ test: import.meta.env.VITE_VDX_TEST === '1' });

export { setBackend } from './bridge';
export { showStartCard, showFatalError } from './web/webstate.svelte';
