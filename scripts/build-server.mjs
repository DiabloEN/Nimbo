import { existsSync, mkdirSync, cpSync, readdirSync, rmSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const web = join(root, 'web');
const windows = process.platform === 'win32';
const localGo = join(root, '.tools', 'go', 'bin', windows ? 'go.exe' : 'go');
const go = existsSync(localGo) ? localGo : 'go';
const env = { ...process.env, GOCACHE: join(root, '.tools', 'go-cache'), GOTOOLCHAIN: 'local' };
function run(command, args, cwd = root) {
  const result = spawnSync(command, args, { cwd, env, stdio: 'inherit', windowsHide: true });
  if (result.error) throw result.error;
  if (result.status !== 0) throw new Error(`${command} failed (${result.status})`);
}

// Invoke Node entry points directly so the same command works on Windows/macOS/Linux.
run(process.execPath, [join(web, 'node_modules/vue-tsc/bin/vue-tsc.js'), '--noEmit'], web);
run(process.execPath, [join(web, 'node_modules/vite/bin/vite.js'), 'build'], web);
const generated = join(root, 'server', 'ui');
mkdirSync(generated, { recursive: true });
// This directory is solely generated; constrain cleanup to its verified absolute path.
for (const name of readdirSync(generated)) {
  if (name === '.keep') continue;
  const target = resolve(generated, name);
  if (dirname(target) !== generated) throw new Error('Invalid generated asset path');
  rmSync(target, { recursive: true, force: true });
}
cpSync(join(web, 'dist'), generated, { recursive: true });
run(go, ['test', './...'], join(root, 'server'));
run(go, ['vet', './...'], join(root, 'server'));
mkdirSync(join(root, 'build-server'), { recursive: true });
const targetOS = env.GOOS || process.platform;
// An alternate output lets a preview build coexist with a running local server.
const output = env.NIMBO_BUILD_OUTPUT
  ? resolve(root, env.NIMBO_BUILD_OUTPUT)
  : join(
      root,
      'build-server',
      targetOS === 'win32' || targetOS === 'windows' ? 'nimbo-server.exe' : 'nimbo-server',
    );
mkdirSync(dirname(output), { recursive: true });
run(go, ['build', '-trimpath', '-o', output, '.'], join(root, 'server'));
console.log(`Built: ${output}`);
