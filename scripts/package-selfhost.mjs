import {
  existsSync,
  mkdirSync,
  mkdtempSync,
  cpSync,
  readdirSync,
  readFileSync,
  writeFileSync,
} from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { execFileSync, spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { checkUIAssets } from './check-ui-assets.mjs';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const web = join(root, 'web');
const artifacts = join(root, 'artifacts', 'selfhost');
mkdirSync(artifacts, { recursive: true });
const work = mkdtempSync(join(artifacts, 'package-'));
const source = join(work, 'source');
const bundle = join(work, 'bundle');
mkdirSync(source);
mkdirSync(bundle);
const localGo = join(root, '.tools', 'go', 'bin', process.platform === 'win32' ? 'go.exe' : 'go');
function run(command, args, cwd, extra = {}) {
  const result = spawnSync(command, args, {
    cwd,
    env: { ...process.env, ...extra },
    stdio: 'inherit',
    windowsHide: true,
  });
  if (result.error) throw result.error;
  if (result.status !== 0) throw Error(`${command} failed: ${result.status}`);
}
checkUIAssets(root);
run(process.execPath, [join(web, 'node_modules/vue-tsc/bin/vue-tsc.js'), '--noEmit'], web);
run(process.execPath, [join(web, 'node_modules/vite/bin/vite.js'), 'build'], web);
checkUIAssets(root, true);
for (const name of readdirSync(join(root, 'server'))) {
  if (name === 'go.mod' || name === 'go.sum' || name.endsWith('.go'))
    cpSync(join(root, 'server', name), join(source, name));
}
cpSync(join(web, 'dist'), join(source, 'ui'), { recursive: true });
const go = existsSync(localGo) ? localGo : 'go';
run(
  go,
  [
    'build',
    '-buildvcs=false',
    '-trimpath',
    '-ldflags=-s -w',
    '-o',
    join(bundle, 'nimbo-server'),
    '.',
  ],
  source,
  {
    GOOS: 'linux',
    GOARCH: 'amd64',
    CGO_ENABLED: '0',
    GOTOOLCHAIN: 'local',
    GOCACHE: join(root, '.tools', 'go-cache'),
  },
);
for (const name of [
  'install.py',
  'host.py',
  'nimbo_cli.py',
  'not_found.py',
  '404.html',
  'nimbo',
  'Dockerfile.binary',
  'README.md',
])
  cpSync(join(root, 'deploy/selfhost', name), join(bundle, name));
mkdirSync(join(bundle, 'antivirus'));
for (const name of ['Dockerfile', 'manager.py', 'healthcheck.py', 'clamd.conf', 'freshclam.conf'])
  cpSync(join(root, 'deploy/antivirus', name), join(bundle, 'antivirus', name));
const notices = join(bundle, 'licenses');
cpSync(join(root, 'LICENSE'), join(bundle, 'LICENSE'));
const exportNotice = join(root, 'scripts/export-docs/THIRD_PARTY.md');
cpSync(
  existsSync(exportNotice) ? exportNotice : join(root, 'THIRD_PARTY.md'),
  join(bundle, 'THIRD_PARTY.md'),
);
const dist = join(web, 'dist');
for (const [from, to] of [
  ['assets/licenses.md', 'web-dependencies.md'],
  ['assets/roboto-license.txt', 'roboto.txt'],
  ['licenses/iconsax.txt', 'iconsax.txt'],
  ['licenses/gtcrn/LICENSE-wasm.txt', 'gtcrn/LICENSE-wasm.txt'],
  ['licenses/rnnoise/COPYING-xiph.txt', 'rnnoise/COPYING-xiph.txt'],
  ['licenses/rnnoise/LICENSE-shiguredo.txt', 'rnnoise/LICENSE-shiguredo.txt'],
])
  cpSync(join(dist, from), join(notices, to));
const worklet = readFileSync(
  join(web, 'node_modules/@sapphi-red/web-noise-suppressor/dist/gtcrn/workletProcessor.js'),
  'utf8',
);
const credits = worklet.match(/\/\*![\s\S]*?\*\//g) ?? [];
if (!['GTCRN', 'pffft'].every((name) => credits.some((block) => block.includes(name))))
  throw Error('Missing GTCRN or pffft notice');
writeFileSync(join(notices, 'gtcrn/model-and-pffft.txt'), credits.join('\n\n') + '\n');
const goRoot = execFileSync(go, ['env', 'GOROOT'], {
  cwd: source,
  env: { ...process.env, GOTOOLCHAIN: 'local' },
  encoding: 'utf8',
}).trim();
for (const name of ['LICENSE', 'PATENTS']) cpSync(join(goRoot, name), join(notices, 'go', name));
const digest = createHash('sha256')
  .update(readFileSync(join(bundle, 'nimbo-server')))
  .digest('hex');
writeFileSync(join(bundle, 'SHA256SUMS'), `${digest}  nimbo-server\n`);
const archive = join(work, 'nimbo-server-linux-amd64.tar.gz');
run(
  'tar',
  [
    '-czf',
    archive,
    '-C',
    bundle,
    'install.py',
    'host.py',
    'nimbo_cli.py',
    'not_found.py',
    '404.html',
    'nimbo',
    'Dockerfile.binary',
    'README.md',
    'nimbo-server',
    'SHA256SUMS',
    'antivirus',
    'LICENSE',
    'THIRD_PARTY.md',
    'licenses',
  ],
  root,
);
console.log(
  `Self-host preview bundle: ${archive}\nServer SHA256: ${digest}\nNot signed; not a public release.`,
);
