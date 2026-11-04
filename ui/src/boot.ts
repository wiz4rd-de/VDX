// Mounts the UI. The production entry (main.ts) and the dev harness
// (tests/harness.ts) both use this; backends are attached with setBackend().
import { mount } from 'svelte';
import './fonts.css';
import './theme.css';
import './app.css';
import App from './App.svelte';

export function boot(target: HTMLElement = document.getElementById('app') ?? document.body): ReturnType<typeof mount> {
  return mount(App, { target });
}
