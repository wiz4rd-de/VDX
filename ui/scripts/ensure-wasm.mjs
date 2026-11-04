// `npm run web` prerequisite: build the WASM core (web/build.sh) when
// ui/public/vdx.wasm is missing. Needs an activated Emscripten SDK.
import { existsSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const ui = fileURLToPath(new URL('..', import.meta.url));
const wasm = `${ui}public/vdx.wasm`;
if (existsSync(wasm) && existsSync(`${ui}public/vdx.js`)) process.exit(0);
const which = spawnSync(process.platform === 'win32' ? 'where' : 'which', ['emcc']);
if (which.status !== 0) {
  console.error('ui/public/vdx.wasm is missing and emcc is not on PATH.');
  console.error('Activate the Emscripten SDK (source <emsdk>/emsdk_env.sh) and run web/build.sh, then npm run web.');
  process.exit(1);
}
const r = spawnSync('bash', [`${ui}../web/build.sh`], { stdio: 'inherit' });
process.exit(r.status ?? 1);
