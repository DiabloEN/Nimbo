import {afterEach,expect,it,vi} from 'vitest';
import {uploadForm} from './uploadForm';
afterEach(()=>vi.unstubAllGlobals());
function setup(){
 const xhr={open:vi.fn(),setRequestHeader:vi.fn(),send:vi.fn(),abort:vi.fn(()=>xhr.onabort()),timeout:0,upload:{} as any,onload:()=>{},onerror:()=>{},ontimeout:()=>{},onabort:()=>{},status:200,responseText:'{"ok":true}'};
 vi.stubGlobal('XMLHttpRequest',class{constructor(){return xhr;}});return xhr;
}
it('reports upload completion separately from the scanner response',async()=>{
 const xhr=setup(),progress=vi.fn(),body=new FormData();const request=uploadForm('/upload',body,'token',new AbortController().signal,progress);
 expect(xhr.setRequestHeader).toHaveBeenCalledWith('Authorization','Bearer token');expect(xhr.send).toHaveBeenCalledWith(body);
 xhr.upload.onprogress({lengthComputable:true,loaded:25,total:100});expect(progress).toHaveBeenLastCalledWith(25);
 xhr.upload.onload();expect(progress).toHaveBeenLastCalledWith(100);
 let finished=false;void request.then(()=>finished=true);await Promise.resolve();expect(finished).toBe(false);
 xhr.onload();expect((await request).ok).toBe(true);
});
it('aborts an in-flight upload on sign-out',async()=>{
 const xhr=setup(),controller=new AbortController();const request=uploadForm('/upload',new FormData(),'token',controller.signal,()=>{});
 const rejected=expect(request).rejects.toThrow('Загрузка прервана');controller.abort();await rejected;expect(xhr.abort).toHaveBeenCalledOnce();
});
