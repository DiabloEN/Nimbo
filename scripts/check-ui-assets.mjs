import { readFileSync, readdirSync } from 'node:fs';
import { join } from 'node:path';
import { createHash } from 'node:crypto';

// Approved camera photograph. Update only with an explicitly approved asset change.
const preview = 'effects/room-preview.png';
const digest = '71f7b6434bcae4846ee3055ea2c93500db457d3f07eba79a8082f0d66e606cf5';
export function checkUIAssets(root, built = false) {
  const directory = join(root, 'web', built ? 'dist' : 'public');
  const actual = createHash('sha256')
    .update(readFileSync(join(directory, preview)))
    .digest('hex');
  if (actual !== digest) throw Error(`Approved camera photograph changed: ${directory}/${preview}`);
  const style = built
    ? readdirSync(join(directory, 'assets'))
        .filter((n) => n.endsWith('.css'))
        .map((n) => readFileSync(join(directory, 'assets', n), 'utf8'))
        .join('\n')
    : readFileSync(join(root, 'web/src/components/EffectPicker.vue'), 'utf8');
  if (!style.includes('/' + preview) || style.includes('/effects/room-preview.svg')) {
    throw Error('Camera effects must reference the approved photograph, not a substitute.');
  }
  console.log(`Approved UI assets verified (${built ? 'built' : 'source'}).`);
}
