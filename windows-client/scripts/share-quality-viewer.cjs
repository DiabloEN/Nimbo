// Receive only the synthetic stream on the disposable loopback server.
const fs = require('node:fs');
const path = require('node:path');
const { chromium } = require(process.env.NIMBO_PLAYWRIGHT || 'playwright');
const [dir, out, browserExe] = process.argv.slice(2);
async function run() {
  const auth = JSON.parse(fs.readFileSync(path.join(dir, 'viewer.json')));
  if (new URL(auth.url).hostname !== '127.0.0.1') throw Error('Loopback fixture required');
  const browser = await chromium.launch({
    executablePath: browserExe,
    headless: true,
    args: ['--mute-audio', '--disable-background-timer-throttling'],
  });
  try {
    const page = await browser.newPage();
    // A real loopback document gives Chrome the correct local-network address space.
    await page.goto(new URL('/', auth.url.replace(/^ws/, 'http')).href);
    await page.evaluate(() => document.body.replaceChildren());
    await page.addScriptTag({
      path: path.resolve('web/node_modules/livekit-client/dist/livekit-client.umd.js'),
    });
    await page.evaluate(async (auth) => {
      const { Room, RoomEvent, Track } = LivekitClient;
      const room = new Room({ adaptiveStream: false });
      window.room = room;
      room.on(RoomEvent.TrackSubscribed, (track) => {
        if (track.source !== Track.Source.ScreenShare) return;
        const v = track.attach();
        v.muted = true;
        document.body.append(v);
        v.play();
        window.video = v;
        window.track = track;
        window.times = [];
        const next = (now, info) => {
          window.times.push(info.presentationTime);
          v.requestVideoFrameCallback(next);
        };
        v.requestVideoFrameCallback(next);
      });
      await room.connect(auth.url, auth.token);
    }, auth);
    await page.waitForFunction(() => window.video?.videoWidth > 0, null, { timeout: 25000 });
    async function image(name) {
      const data = await page.evaluate(() => {
        const c = document.createElement('canvas');
        c.width = video.videoWidth;
        c.height = video.videoHeight;
        c.getContext('2d').drawImage(video, 0, 0);
        return c.toDataURL('image/png').split(',')[1];
      });
      fs.writeFileSync(path.join(out, name + '.png'), Buffer.from(data, 'base64'));
    }
    async function stats() {
      return page.evaluate(async () => {
        const report = await track.receiver.getStats();
        const s = [...report.values()].find((s) => s.type === 'inbound-rtp' && s.kind === 'video');
        return {
          ts: s.timestamp,
          bytes: s.bytesReceived,
          decoded: s.framesDecoded,
          dropped: s.framesDropped,
          freezes: s.freezeCount,
          lost: s.packetsLost,
          width: s.frameWidth,
          height: s.frameHeight,
          qp: s.qpSum,
        };
      });
    }
    await page.waitForTimeout(3000);
    await image('received-static');
    fs.writeFileSync(path.join(dir, 'motion'), '1');
    await page.waitForTimeout(7000);
    const before = await stats();
    await page.evaluate(() => {
      window.times = [];
    });
    await page.waitForTimeout(10000);
    const after = await stats(),
      times = await page.evaluate(() => window.times.slice());
    for (let i = 0; i < 4; i++) {
      await image('received-motion-' + i);
      await page.waitForTimeout(310);
    }
    fs.writeFileSync(path.join(out, 'receiver.json'), JSON.stringify({ before, after, times }));
  } finally {
    await browser.close();
  }
}
run().catch((e) => {
  console.error(e);
  process.exitCode = 1;
});
