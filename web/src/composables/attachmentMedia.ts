// Only passive browser media. Never embed HTML, SVG, PDFs or arbitrary documents.
const formats:Record<string,{kind:'image'|'audio'|'video';mime:string}>={
 png:{kind:'image',mime:'image/png'},jpg:{kind:'image',mime:'image/jpeg'},jpeg:{kind:'image',mime:'image/jpeg'},gif:{kind:'image',mime:'image/gif'},webp:{kind:'image',mime:'image/webp'},avif:{kind:'image',mime:'image/avif'},
 mp3:{kind:'audio',mime:'audio/mpeg'},wav:{kind:'audio',mime:'audio/wav'},ogg:{kind:'audio',mime:'audio/ogg'},oga:{kind:'audio',mime:'audio/ogg'},flac:{kind:'audio',mime:'audio/flac'},m4a:{kind:'audio',mime:'audio/mp4'},
 mp4:{kind:'video',mime:'video/mp4'},webm:{kind:'video',mime:'video/webm'},ogv:{kind:'video',mime:'video/ogg'}
};
export function attachmentMedia(name:string){return formats[name.split('.').at(-1)?.toLowerCase()||''];}

export async function validMediaHeader(blob:Blob,name:string):Promise<boolean>{
 const media=attachmentMedia(name);if(!media)return false;
 const b=new Uint8Array(await blob.slice(0,65536).arrayBuffer()),s=(start:number,end:number)=>String.fromCharCode(...b.slice(start,end));
 const view=new DataView(b.buffer);const dimensions=(w:number,h:number)=>w>0&&h>0&&w<=8192&&h<=8192&&w*h<=24000000;
 switch(media.mime){
  case 'image/png':return b.length>=24&&b[0]===137&&s(1,8)==='PNG\r\n\x1a\n'&&dimensions(view.getUint32(16),view.getUint32(20));
  case 'image/gif':return b.length>=10&&['GIF87a','GIF89a'].includes(s(0,6))&&dimensions(view.getUint16(6,true),view.getUint16(8,true));
  case 'image/jpeg':{
   if(b[0]!==255||b[1]!==216)return false;
   let i=2;while(i+4<b.length){if(b[i++]!==255)return false;while(b[i]===255)i++;const marker=b[i++]!;if(marker===217||marker===218||i+2>b.length)return false;
    const length=view.getUint16(i);if(length<2||i+length>b.length)return false;
    if([192,193,194,195,197,198,199,201,202,203,205,206,207].includes(marker))return length>=7&&dimensions(view.getUint16(i+5),view.getUint16(i+3));i+=length;
   }return false;
  }
  case 'image/webp':{
   if(b.length<30||s(0,4)!=='RIFF'||s(8,12)!=='WEBP')return false;
   if(s(12,16)==='VP8X')return dimensions(1+b[24]!+(b[25]!<<8)+(b[26]!<<16),1+b[27]!+(b[28]!<<8)+(b[29]!<<16));
   if(s(12,16)==='VP8 ')return b[23]===157&&b[24]===1&&b[25]===42&&dimensions(view.getUint16(26,true)&16383,view.getUint16(28,true)&16383);
   if(s(12,16)==='VP8L'&&b[20]===47)return dimensions(1+b[21]!+((b[22]!&63)<<8),1+(b[22]!>>6)+(b[23]!<<2)+((b[24]!&15)<<10));return false;
  }
  // AVIF needs a full container parser; keep it downloadable without automatic decoding.
  case 'image/avif':return false;
  case 'audio/mpeg':return s(0,3)==='ID3'||b[0]===255&&(b[1]!&224)===224;
  case 'audio/wav':return s(0,4)==='RIFF'&&s(8,12)==='WAVE';
  case 'audio/ogg':case 'video/ogg':return s(0,4)==='OggS';
  case 'audio/flac':return s(0,4)==='fLaC';
  case 'audio/mp4':case 'video/mp4':return s(4,8)==='ftyp';
  case 'video/webm':return b[0]===26&&b[1]===69&&b[2]===223&&b[3]===163;
  default:return false;
 }
}
