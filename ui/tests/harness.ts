// Development harness: the real UI driven by the test-double backend.
// Open http://localhost:5173/tests/harness.html?preset=edit21&overlay=bank
// Query parameters: preset (play|edit21|func17|edited|blank), build, midi,
// idb=0, audio=0, crt=0, overlay (bank|settings), start=1, fatal=1, zoom.
import { boot } from '../src/boot';
import { setBackend } from '../src/bridge';
import { store } from '../src/store.svelte';
import { showStartCard, showFatalError } from '../src/web/webstate.svelte';
import { FakeBackend, type Preset } from './fake-backend';
import type { BuildKind } from '../src/protocol';

const q = new URLSearchParams(location.search);
const fake = new FakeBackend({
  preset: (q.get('preset') as Preset) ?? 'play',
  build: (q.get('build') as BuildKind) ?? 'web',
  midi: (q.get('midi') as 'ok' | 'unavailable' | 'nosysex') ?? 'ok',
  idb: q.get('idb') !== '0',
  audio: q.get('audio') !== '0',
  cartridge: q.get('crt') !== '0',
});
if (q.get('zoom')) fake.settings.zoom = Number(q.get('zoom'));
if (q.get('fatal')) showFatalError('VDX needs to be opened from http://localhost:5173 (cross-origin isolation is off).');
boot();
setBackend(fake);
if (q.get('start')) void showStartCard();
const overlay = q.get('overlay');
if (overlay === 'bank' || overlay === 'settings') store.overlay = overlay;
(window as unknown as { __fake: FakeBackend; __store: typeof store }).__fake = fake;
(window as unknown as { __store: typeof store }).__store = store;
