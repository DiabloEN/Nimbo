// Run against the disposable loopback server from native_qa_services.
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const { chromium } = require(process.env.NIMBO_PLAYWRIGHT || 'playwright');
const baseURL = process.env.NIMBO_NATIVE_TEST_URL;
const dir = process.env.NIMBO_BROWSER_AUDIO_FIXTURE;
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));

async function main() {
  assert.equal(new URL(baseURL).hostname, '127.0.0.1');
  const browser = await chromium.launch({
    executablePath: process.env.NIMBO_QA_BROWSER, headless: true,
    args: ['--mute-audio', '--use-fake-device-for-media-stream', '--use-fake-ui-for-media-stream',
      '--use-file-for-fake-audio-capture=' + path.resolve('desktop/sounds/voice-camera.wav')],
  });
  try {
    const ctx = await browser.newContext({ baseURL, locale: 'ru-RU', permissions: ['microphone'] });
    async function post(url, data, token) {
      const res = await ctx.request.post(url, { data, headers: token ? { Authorization: 'Bearer ' + token } : {} });
      assert.ok(res.ok(), url + ' ' + res.status());
      return res.json();
    }
    const host = await post('/api/auth', {name:'Browser', password:process.env.NIMBO_NATIVE_TEST_CODE});
    const invite = await post('/api/invitations', {seconds:300,singleUse:true},host.session);
    const guest = await post('/api/auth',{name:'Receiver',password:invite.code});
    await ctx.addInitScript(key => localStorage.setItem('nimbo-profile-key',key),host.profileKey);
    await ctx.addInitScript(() => {
      // Use a generated tab with a silent soundtrack. Only the microphone can make the mix audible.
      navigator.mediaDevices.getDisplayMedia = async () => {
        const canvas = document.createElement('canvas');
        canvas.width = 320; canvas.height = 180;
        const paint = canvas.getContext('2d');
        const timer = setInterval(() => { paint.fillStyle = Date.now()%200 < 100 ? 'orange' : 'green'; paint.fillRect(0,0,320,180); },50);
        const stream = canvas.captureStream(15);
        const track = stream.getVideoTracks()[0];
        const settings = track.getSettings.bind(track);
        track.getSettings = () => ({...settings(),displaySurface:'browser'});
        if(window.includeTabAudio){
          const audio = new AudioContext();
          window.audioContexts.push(audio);
          stream.addTrack(audio.createMediaStreamDestination().stream.getAudioTracks()[0]);
        }
        const stop = track.stop.bind(track);
        track.stop = () => { clearInterval(timer);stop(); };
        return stream;
      };
      window.audioContexts=[];
    });
    const page = await ctx.newPage();
    await page.goto('/#/room');
    await page.getByRole('button',{name:'Подключиться: Гостиная',exact:true}).click();
    await page.getByRole('button',{name:'Выйти из комнаты: Гостиная',exact:true}).waitFor();
    const receiver = await post('/api/join',{room:'lounge',name:'Receiver'},guest.session);
    fs.writeFileSync(path.join(dir,'receiver.tmp'),JSON.stringify(receiver));
    fs.renameSync(path.join(dir,'receiver.tmp'),path.join(dir,'receiver.json'));
    for(let stage=0;stage<=10;++stage){
      if(stage%2){
        await page.evaluate(value => window.includeTabAudio=value,stage%4===3);
        await page.getByRole('button',{name:'Показать экран',exact:true}).click();
        await page.getByRole('button',{name:'Остановить показ экрана',exact:true}).waitFor();
      }else if(stage){
        await page.getByRole('button',{name:'Остановить показ экрана',exact:true}).click();
        await page.getByRole('button',{name:'Показать экран',exact:true}).waitFor();
      }
      fs.writeFileSync(path.join(dir,'stage-'+stage),'');
      const deadline=Date.now()+20000;
      while(!fs.existsSync(path.join(dir,'heard-'+stage))){
        assert.ok(Date.now()<deadline,'No microphone playout at stage '+stage);
        await sleep(100);
      }
    }
  } finally { await browser.close(); }
}
main().catch(err=>{console.error(err.message);process.exitCode=1;});
