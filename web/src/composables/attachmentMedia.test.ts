import {expect,it} from 'vitest';
import {attachmentMedia,validMediaHeader} from './attachmentMedia';
it('allows passive image/audio/video formats case-insensitively',()=>{expect(attachmentMedia('Photo.JPG')?.kind).toBe('image');expect(attachmentMedia('voice.mp3')?.kind).toBe('audio');expect(attachmentMedia('clip.webm')?.kind).toBe('video');});
it('never embeds active documents or disguised extensions',()=>{for(const name of ['file.svg','page.html','doc.pdf','image.jpg.html','archive.zip','unknown'])expect(attachmentMedia(name)).toBeUndefined();});
it('rejects HTML renamed to supported media',async()=>{for(const name of ['fake.jpg','fake.png','fake.mp4','fake.mp3'])expect(await validMediaHeader(new Blob(['<html><script>alert(1)</script>']),name)).toBe(false);});
it('rejects oversized decoded image dimensions',async()=>{const b=new Uint8Array(24);b.set([137,80,78,71,13,10,26,10]);const v=new DataView(b.buffer);v.setUint32(16,100000);v.setUint32(20,100000);expect(await validMediaHeader(new Blob([b]),'bomb.png')).toBe(false);v.setUint32(16,640);v.setUint32(20,480);expect(await validMediaHeader(new Blob([b]),'photo.png')).toBe(true);});
