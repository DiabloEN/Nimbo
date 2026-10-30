// Render links as Vue text/attributes, never as HTML. No preview fetches or trackers.
export function linkParts(text:string):{text:string;href?:string}[] {
  const parts:{text:string;href?:string}[]=[];let offset=0;
  for(const match of text.matchAll(/(?:https?:\/\/|www\.)[^\s<>"\u0000-\u001f]+/giu)) {
    const start=match.index!;
    if(start>0&&/[\p{L}\p{N}_/@]/u.test(text[start-1]!))continue;
    let value=match[0].replace(/[.,!?;:…]+$/u,'');
    for(const [open,close] of [['(',')'],['[',']'],['{','}']]) {
      while(value.endsWith(close!)&&value.split(close!).length>value.split(open!).length)value=value.slice(0,-1);
    }
    let url:URL;try{url=new URL(value.startsWith('www.')?'https://'+value:value);}catch{continue;}
    if(!['http:','https:'].includes(url.protocol)||url.username||url.password)continue;
    if(start>offset)parts.push({text:text.slice(offset,start)});
    parts.push({text:value,href:url.href});offset=start+value.length;
  }
  if(offset<text.length)parts.push({text:text.slice(offset)});
  return parts;
}
