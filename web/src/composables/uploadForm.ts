/** Progress describes transfer; 100% means the server is still validating/scanning. */
export function uploadForm(url:string,body:FormData,token:string,signal:AbortSignal,progress:(percent:number)=>void):Promise<Response>{
 return new Promise((resolve,reject)=>{
  const xhr=new XMLHttpRequest();
  const abort=()=>xhr.abort();
  const finish=()=>signal.removeEventListener('abort',abort);
  xhr.open('POST',url);xhr.setRequestHeader('Authorization','Bearer '+token);xhr.timeout=240000;
  xhr.upload.onprogress=e=>{if(e.lengthComputable)progress(Math.min(100,Math.round(e.loaded/e.total*100)));};
  xhr.upload.onload=()=>progress(100);
  xhr.onload=()=>{finish();resolve(new Response(xhr.responseText,{status:xhr.status,headers:{'Content-Type':'application/json'}}));};
  xhr.onerror=xhr.ontimeout=xhr.onabort=()=>{finish();reject(new Error('Загрузка прервана. Повторите отправку.'));};
  signal.addEventListener('abort',abort,{once:true});
  if(signal.aborted){finish();reject(new Error('Запрос отменён.'));return;}
  xhr.send(body);
 });
}
