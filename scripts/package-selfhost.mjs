import {existsSync, mkdirSync, mkdtempSync, cpSync, readdirSync, readFileSync, writeFileSync} from 'node:fs';
import {resolve, dirname, join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const web = join(root, 'web');
const artifacts = join(root, 'artifacts', 'selfhost');
mkdirSync(artifacts, {recursive: true});
const work = mkdtempSync(join(artifacts, 'package-'));
const source = join(work, 'source');
const bundle = join(work, 'bundle');
mkdirSync(source); mkdirSync(bundle);
const localGo = join(root, '.tools', 'go', 'bin', process.platform === 'win32' ? 'go.exe' : 'go');
function run(command, args, cwd, extra = {}) {
  const result = spawnSync(command, args, {cwd, env: {...process.env, ...extra}, stdio: 'inherit', windowsHide: true});
  if (result.error) throw result.error;
  if (result.status !== 0) throw Error(`${command} failed: ${result.status}`);
}
run(process.execPath, [join(web, 'node_modules/vue-tsc/bin/vue-tsc.js'), '--noEmit'], web);
run(process.execPath, [join(web, 'node_modules/vite/bin/vite.js'), 'build'], web);
for (const name of readdirSync(join(root, 'server'))) {
  if (name === 'go.mod' || name === 'go.sum' || name.endsWith('.go')) cpSync(join(root, 'server', name), join(source, name));
}
cpSync(join(web, 'dist'), join(source, 'ui'), {recursive: true});
run(existsSync(localGo) ? localGo : 'go', ['build', '-buildvcs=false', '-trimpath', '-ldflags=-s -w', '-o', join(bundle, 'nimbo-server'), '.'], source,
    {GOOS: 'linux', GOARCH: 'amd64', CGO_ENABLED: '0', GOTOOLCHAIN: 'local', GOCACHE: join(root, '.tools', 'go-cache')});
for (const name of ['install.py', 'host.py', 'not_found.py', '404.html', 'nimbo', 'Dockerfile.binary', 'README.md']) cpSync(join(root, 'deploy/selfhost', name), join(bundle, name));
mkdirSync(join(bundle, 'antivirus'));
for (const name of ['Dockerfile', 'manager.py', 'healthcheck.py', 'clamd.conf', 'freshclam.conf']) cpSync(join(root, 'deploy/antivirus', name), join(bundle, 'antivirus', name));
const digest = createHash('sha256').update(readFileSync(join(bundle, 'nimbo-server'))).digest('hex');
writeFileSync(join(bundle, 'SHA256SUMS'), `${digest}  nimbo-server\n`);
const archive = join(work, 'nimbo-server-linux-amd64.tar.gz');
run('tar', ['-czf', archive, '-C', bundle, 'install.py', 'host.py', 'not_found.py', '404.html', 'nimbo', 'Dockerfile.binary', 'README.md', 'nimbo-server', 'SHA256SUMS', 'antivirus'], root);
console.log(`Self-host preview bundle: ${archive}\nServer SHA256: ${digest}\nNot signed; not a public release.`);
