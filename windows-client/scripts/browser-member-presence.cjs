// Paired with NativeDesktopTests::memberPresenceCrossClient on disposable loopback servers.
const { chromium } = require(process.env.NIMBO_PLAYWRIGHT || 'playwright');
const fs = require('node:fs/promises');
const path = require('node:path');
const assert = require('node:assert/strict');
const { setTimeout: sleep } = require('node:timers/promises');
const dir = process.env.NIMBO_QA_PRESENCE_DIR;
const output = process.env.NIMBO_QA_PRESENCE_OUTPUT;
const origin = process.env.NIMBO_NATIVE_TEST_URL;
assert.equal(new URL(origin).hostname, '127.0.0.1');
const marks = {};
const mark = async name => {
  await fs.writeFile(path.join(dir,name),'ok');
  marks[name]=Date.now();
};
async function wait(name){
  const limit=Date.now()+30000;
  while(Date.now()<limit){
    try{return await fs.readFile(path.join(dir,name),'utf8');}
    catch(err){if(err.code!=='ENOENT')throw err;}
    await sleep(50);
  }
  throw Error('Timed out waiting for '+name);
}
(async()=>{
  const browser=await chromium.launch({channel:'chrome',headless:true,args:['--mute-audio']});
  try{
    const ctx=await browser.newContext({baseURL:origin,viewport:{width:1300,height:920}});
    const login=await ctx.request.post('/api/auth',{data:{name:'Web Observer',password:await wait('guest')}});
    assert.ok(login.ok(),'enroll browser observer HTTP '+login.status());
    let voiceJoins=0;
    ctx.on('request',req=>{if(new URL(req.url()).pathname==='/api/join')++voiceJoins;});
    const viewer=await ctx.newPage();
    await viewer.goto('/#/room',{waitUntil:'domcontentloaded'});
    const panel=viewer.locator('.members-panel');
    const toggle=panel.locator('.members-toggle');
    if(await toggle.getAttribute('aria-expanded')!=='true')await toggle.click();
    const row=panel.locator('li').filter({has:viewer.getByText('Desktop Observer',{exact:true})});
    const dot=row.getByRole('img',{name:'В сети'});
    await dot.waitFor();
    // The roster expands horizontally before its vertical animation brings both rows into view.
    await viewer.waitForFunction(()=>{
      const panel=document.querySelector('.members-panel.expanded');
      return panel && panel.getBoundingClientRect().height>200 &&
        [...panel.querySelectorAll('li')].every(row=>row.getBoundingClientRect().bottom<panel.getBoundingClientRect().bottom);
    });
    await panel.screenshot({path:path.join(output,'web-online.png'),animations:'disabled'});
    await mark('web-sees-win');
    await wait('win-sees-web');
    await ctx.setOffline(true);
    await mark('web-disconnected');
    await wait('win-sees-web-off');
    await ctx.setOffline(false);
    await wait('win-sees-web-back');
    await wait('win-disconnected');
    await dot.waitFor({state:'detached',timeout:20000});await row.waitFor();
    await mark('web-sees-win-offline');
    await dot.waitFor({timeout:20000});
    await mark('web-sees-win-recovered');
    await wait('switched');
    await dot.waitFor({state:'detached',timeout:20000});await row.waitFor();
    await mark('web-sees-switch');
    await dot.waitFor({timeout:20000});
    await mark('web-sees-return');
    await wait('win-closed');
    await dot.waitFor({state:'detached',timeout:20000});await row.waitFor();
    assert.equal(voiceJoins,0);
    await mark('web-sees-close');
    await fs.writeFile(path.join(output,'browser-presence.json'),JSON.stringify({voiceJoins,marks},null,2));
  }finally{await browser.close();}
})().catch(err=>{console.error(err);process.exitCode=1;});
