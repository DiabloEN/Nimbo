import { mkdir, readFile } from 'node:fs/promises';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {createRequire} from 'node:module';

const [webDir, outputDir] = process.argv.slice(2);
const require = createRequire(import.meta.url);
const {chromium} = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const load = file => import(pathToFileURL(path.resolve(webDir, file)).href);
const {iconsax} = await load('src/composables/iconsax.ts');
const {channelIcons} = await load('src/composables/channelIcons.ts');
const css = await readFile(path.join(webDir,'src/style.css'),'utf8');
await mkdir(outputDir,{recursive:true});
const browser = await chromium.launch({channel:'chrome',headless:true});
try {
  for (const scale of [1,2]) {
    const page = await browser.newPage({viewport:{width:1280,height:780},deviceScaleFactor:scale});
    await page.setContent('<nav class="channels"></nav>');
    await page.addStyleTag({content:css.replace(/^@import[^;]*;/m,'')});
    await page.addStyleTag({content:`
      body {background:var(--sidebar)}
      .channels {width:64px;gap:0}
      .voice-channel-row[data-hover] {background:#303030}
    `});
    for (const {id} of channelIcons) {
      await page.evaluate(nodes => {
        const nav = document.querySelector('nav');
        nav.replaceChildren();
        for (const state of ['idle','hover','selected','drop']) {
          const item = document.createElement('div');
          item.className = 'channel-item' + (state==='drop'?' voice-drop-target':'');
          const row = document.createElement('div');
          row.className = 'voice-channel-row' + (state==='selected'?' selected':'');
          if (state==='hover') row.dataset.hover='';
          const button = document.createElement('button');
          button.className='voice-channel-open';
          const svg = document.createElementNS('http://www.w3.org/2000/svg','svg');
          for(const [key,value] of Object.entries({width:18,height:18,viewBox:'0 0 24 24',fill:'none'}))svg.setAttribute(key,value);
          for(const {tag,attrs} of nodes){
            const part=document.createElementNS(svg.namespaceURI,tag);
            for(const [key,value] of Object.entries(attrs))part.setAttribute(key,value);
            svg.append(part);
          }
          button.append(svg);row.append(button);item.append(row);nav.append(item);
        }
      },iconsax[id]);
      await page.locator('nav').screenshot({path:path.join(outputDir,id+'@'+scale+'.png')});
    }
    await page.close();
  }
} finally { await browser.close(); }
